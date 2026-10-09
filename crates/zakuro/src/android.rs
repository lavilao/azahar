//! where an Android app's files live, asked of the system through JNI, and
//! the XDG variables that point the rest of the program there. the app's
//! folder on shared storage needs no permission and shows up over USB, which
//! is where games and the libraries 3dsrecomp made for them go.

use std::path::PathBuf;
use std::sync::OnceLock;

/// this has to match the package in AndroidManifest.xml.
pub const PACKAGE: &str = "io.github.lavilao.zakuro";

static EXTERNAL_FILES: OnceLock<Option<PathBuf>> = OnceLock::new();

/// the app's folder on shared storage, no permission needed.
pub fn external_files_dir() -> Option<PathBuf> {
    EXTERNAL_FILES.get_or_init(|| {
        if let Some(dir) = jni_external_files_dir() {
            return Some(dir);
        }
        // no Context to ask (or JNI failed): the folder has a fixed place,
        // which on every phone there is
        let candidates = [
            format!("/storage/emulated/0/Android/data/{PACKAGE}/files"),
            format!("/sdcard/Android/data/{PACKAGE}/files"),
        ];
        candidates
            .iter()
            .map(PathBuf::from)
            .find(|dir| std::fs::create_dir_all(dir).is_ok())
    })
    .clone()
}

/// Context.getExternalFilesDir(None), the app's own folder on the storage.
fn jni_external_files_dir() -> Option<PathBuf> {
    // android-activity put the JavaVM and the application Context where any
    // crate can find them, before android_main ran
    let context = ndk_context::android_context();
    let vm = unsafe { jni::JavaVM::from_raw(context.vm().cast()) }.ok()?;
    let mut env = vm.attach_current_thread().ok()?;
    let context = unsafe { jni::objects::JObject::from_raw(context.context() as _) };
    let file = env
        .call_method(&context, "getExternalFilesDir", "()Ljava/io/File;", &[])
        .ok()?
        .l()
        .ok()?;
    let path = env
        .call_method(&file, "getAbsolutePath", "()Ljava/lang/String;", &[])
        .ok()?
        .l()
        .ok()?;
    let path = unsafe { jni::objects::JString::from_raw(path.as_raw() as _) };
    let path: String = env.get_string(&path).ok()?.into();
    (!path.is_empty()).then(|| PathBuf::from(path))
}

/// points the settings, the saves and 3dsrecomp's libraries at the app's
/// folder, before anything looks for them.
pub fn init() {
    if let Some(dir) = external_files_dir() {
        let _ = std::fs::create_dir_all(&dir);
        // these make the system's-place-for-data lookups land there, the
        // settings in <dir>/zakuro/settings.toml and the libraries a PC
        // compiled in <dir>/3dsrecomp
        std::env::set_var("XDG_DATA_HOME", &dir);
        std::env::set_var("XDG_CONFIG_HOME", &dir);
        log::info!("keeping Zakuro's files in {}", dir.display());
    } else {
        log::warn!("could not find the app's folder on the storage");
    }
}

/// where the library looks for games, <files>/games.
pub fn games_dir() -> Option<PathBuf> {
    external_files_dir().map(|dir| dir.join("games"))
}
