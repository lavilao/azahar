// Carga perezosa de las bibliotecas del NDK de camara (libcamera2ndk y
// libmediandk).
//
// Motivo: al enlazarlas directamente, el cargador dinamico tira de todo el
// stack del framework de camara (libcamera_client, libgui, libbinder, ...)
// AL ARRANCAR la app, con su inicializacion estatica incluida. En un Redmi 9A
// (MIUI, ARM32) eso es peso innecesario en el arranque y superficie donde han
// aparecido cierres senal 7 nada mas abrir la app. La camara real solo se usa
// en el applet de camara de un juego, asi que se cargan con dlopen la primera
// vez que se necesitan.
//
// Como: la tabla guarda punteros con el tipo exacto de la funcion real
// (decltype del prototipo del NDK) y macros con el mismo nombre reescriben
// las llamadas, de modo que ndk_camera.cpp no cambia. Si falta alguna
// funcion, el enlazado falla en CI (no quedan referencias directas).

#pragma once

#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadata.h>
#include <camera/NdkCaptureRequest.h>
#include <dlfcn.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>

#include "common/logging/log.h"

namespace Camera::NDK {

struct LazyFuncTable {
    // libcamera2ndk
    decltype(&ACameraManager_create) ACameraManager_create_ptr;
    decltype(&ACameraManager_delete) ACameraManager_delete_ptr;
    decltype(&ACameraManager_openCamera) ACameraManager_openCamera_ptr;
    decltype(&ACameraManager_getCameraCharacteristics) ACameraManager_getCameraCharacteristics_ptr;
    decltype(&ACameraManager_getCameraIdList) ACameraManager_getCameraIdList_ptr;
    decltype(&ACameraManager_deleteCameraIdList) ACameraManager_deleteCameraIdList_ptr;
    decltype(&ACameraMetadata_getConstEntry) ACameraMetadata_getConstEntry_ptr;
    decltype(&ACameraMetadata_free) ACameraMetadata_free_ptr;
    decltype(&ACameraDevice_close) ACameraDevice_close_ptr;
    decltype(&ACameraDevice_createCaptureSession) ACameraDevice_createCaptureSession_ptr;
    decltype(&ACameraDevice_createCaptureRequest) ACameraDevice_createCaptureRequest_ptr;
    decltype(&ACaptureSessionOutputContainer_create) ACaptureSessionOutputContainer_create_ptr;
    decltype(&ACaptureSessionOutputContainer_add) ACaptureSessionOutputContainer_add_ptr;
    decltype(&ACaptureSessionOutputContainer_free) ACaptureSessionOutputContainer_free_ptr;
    decltype(&ACaptureSessionOutput_create) ACaptureSessionOutput_create_ptr;
    decltype(&ACaptureSessionOutput_free) ACaptureSessionOutput_free_ptr;
    decltype(&ACameraOutputTarget_create) ACameraOutputTarget_create_ptr;
    decltype(&ACameraOutputTarget_free) ACameraOutputTarget_free_ptr;
    decltype(&ACaptureRequest_free) ACaptureRequest_free_ptr;
    decltype(&ACaptureRequest_addTarget) ACaptureRequest_addTarget_ptr;
    decltype(&ACameraCaptureSession_close) ACameraCaptureSession_close_ptr;
    decltype(&ACameraCaptureSession_setRepeatingRequest)
        ACameraCaptureSession_setRepeatingRequest_ptr;

    // libmediandk
    decltype(&AImageReader_new) AImageReader_new_ptr;
    decltype(&AImageReader_delete) AImageReader_delete_ptr;
    decltype(&AImageReader_setImageListener) AImageReader_setImageListener_ptr;
    decltype(&AImageReader_getWindow) AImageReader_getWindow_ptr;
    decltype(&AImageReader_acquireLatestImage) AImageReader_acquireLatestImage_ptr;
    decltype(&AImage_delete) AImage_delete_ptr;
    decltype(&AImage_getPlaneData) AImage_getPlaneData_ptr;
    decltype(&AImage_getPlaneRowStride) AImage_getPlaneRowStride_ptr;
    decltype(&AImage_getPlanePixelStride) AImage_getPlanePixelStride_ptr;

    bool ok = false;
};

namespace lazy_impl {

template <typename T>
void LoadSym(void* handle, T& fn, const char* name) {
    if (handle == nullptr) {
        return;
    }
    fn = reinterpret_cast<T>(dlsym(handle, name));
}

inline const LazyFuncTable& LoadTable() {
    static LazyFuncTable* table = [] {
        auto* t = new LazyFuncTable{};

        void* camera_lib = dlopen("libcamera2ndk.so", RTLD_LAZY | RTLD_LOCAL);
        if (camera_lib == nullptr) {
            LOG_ERROR(Service_CAM, "no se pudo abrir libcamera2ndk: {}", dlerror());
        }
        void* media_lib = dlopen("libmediandk.so", RTLD_LAZY | RTLD_LOCAL);
        if (media_lib == nullptr) {
            LOG_ERROR(Service_CAM, "no se pudo abrir libmediandk: {}", dlerror());
        }

        LoadSym(camera_lib, t->ACameraManager_create_ptr, "ACameraManager_create");
        LoadSym(camera_lib, t->ACameraManager_delete_ptr, "ACameraManager_delete");
        LoadSym(camera_lib, t->ACameraManager_openCamera_ptr, "ACameraManager_openCamera");
        LoadSym(camera_lib, t->ACameraManager_getCameraCharacteristics_ptr,
                "ACameraManager_getCameraCharacteristics");
        LoadSym(camera_lib, t->ACameraManager_getCameraIdList_ptr,
                "ACameraManager_getCameraIdList");
        LoadSym(camera_lib, t->ACameraManager_deleteCameraIdList_ptr,
                "ACameraManager_deleteCameraIdList");
        LoadSym(camera_lib, t->ACameraMetadata_getConstEntry_ptr,
                "ACameraMetadata_getConstEntry");
        LoadSym(camera_lib, t->ACameraMetadata_free_ptr, "ACameraMetadata_free");
        LoadSym(camera_lib, t->ACameraDevice_close_ptr, "ACameraDevice_close");
        LoadSym(camera_lib, t->ACameraDevice_createCaptureSession_ptr,
                "ACameraDevice_createCaptureSession");
        LoadSym(camera_lib, t->ACameraDevice_createCaptureRequest_ptr,
                "ACameraDevice_createCaptureRequest");
        LoadSym(camera_lib, t->ACaptureSessionOutputContainer_create_ptr,
                "ACaptureSessionOutputContainer_create");
        LoadSym(camera_lib, t->ACaptureSessionOutputContainer_add_ptr,
                "ACaptureSessionOutputContainer_add");
        LoadSym(camera_lib, t->ACaptureSessionOutputContainer_free_ptr,
                "ACaptureSessionOutputContainer_free");
        LoadSym(camera_lib, t->ACaptureSessionOutput_create_ptr, "ACaptureSessionOutput_create");
        LoadSym(camera_lib, t->ACaptureSessionOutput_free_ptr, "ACaptureSessionOutput_free");
        LoadSym(camera_lib, t->ACameraOutputTarget_create_ptr, "ACameraOutputTarget_create");
        LoadSym(camera_lib, t->ACameraOutputTarget_free_ptr, "ACameraOutputTarget_free");
        LoadSym(camera_lib, t->ACaptureRequest_free_ptr, "ACaptureRequest_free");
        LoadSym(camera_lib, t->ACaptureRequest_addTarget_ptr, "ACaptureRequest_addTarget");
        LoadSym(camera_lib, t->ACameraCaptureSession_close_ptr, "ACameraCaptureSession_close");
        LoadSym(camera_lib, t->ACameraCaptureSession_setRepeatingRequest_ptr,
                "ACameraCaptureSession_setRepeatingRequest");

        LoadSym(media_lib, t->AImageReader_new_ptr, "AImageReader_new");
        LoadSym(media_lib, t->AImageReader_delete_ptr, "AImageReader_delete");
        LoadSym(media_lib, t->AImageReader_setImageListener_ptr, "AImageReader_setImageListener");
        LoadSym(media_lib, t->AImageReader_getWindow_ptr, "AImageReader_getWindow");
        LoadSym(media_lib, t->AImageReader_acquireLatestImage_ptr,
                "AImageReader_acquireLatestImage");
        LoadSym(media_lib, t->AImage_delete_ptr, "AImage_delete");
        LoadSym(media_lib, t->AImage_getPlaneData_ptr, "AImage_getPlaneData");
        LoadSym(media_lib, t->AImage_getPlaneRowStride_ptr, "AImage_getPlaneRowStride");
        LoadSym(media_lib, t->AImage_getPlanePixelStride_ptr, "AImage_getPlanePixelStride");

        t->ok = camera_lib != nullptr && media_lib != nullptr &&
                t->ACameraManager_create_ptr != nullptr && t->AImageReader_new_ptr != nullptr &&
                t->ACameraDevice_close_ptr != nullptr && t->AImage_delete_ptr != nullptr;
        if (!t->ok) {
            LOG_ERROR(Service_CAM, "la camara NDK no esta disponible en este dispositivo");
        }
        return t;
    }();
    return *table;
}

} // namespace lazy_impl

/// tabla de funciones de la camara (carga las bibliotecas la primera vez)
inline const LazyFuncTable& Cam() {
    return lazy_impl::LoadTable();
}

/// ¿esta disponible la camara NDK? (sin forzar la carga)
inline bool CamAvailable() {
    return Cam().ok;
}

} // namespace Camera::NDK

// Las macros se definen DESPUES de la tabla para que decltype(&funcion) vea
// las declaraciones reales del NDK. Con esto, todo el codigo existente que
// llama a estas funciones sigue compilando igual pero pasa por dlsym.
#define ACameraManager_create() (Camera::NDK::Cam().ACameraManager_create_ptr())
#define ACameraManager_delete(manager) (Camera::NDK::Cam().ACameraManager_delete_ptr(manager))
#define ACameraManager_openCamera(manager, id, callbacks, device)                            \
    (Camera::NDK::Cam().ACameraManager_openCamera_ptr(manager, id, callbacks, device))
#define ACameraManager_getCameraCharacteristics(manager, id, metadata)                       \
    (Camera::NDK::Cam().ACameraManager_getCameraCharacteristics_ptr(manager, id, metadata))
#define ACameraManager_getCameraIdList(manager, idList)                                      \
    (Camera::NDK::Cam().ACameraManager_getCameraIdList_ptr(manager, idList))
#define ACameraManager_deleteCameraIdList(idList)                                            \
    (Camera::NDK::Cam().ACameraManager_deleteCameraIdList_ptr(idList))
#define ACameraMetadata_getConstEntry(metadata, tag, entry)                                  \
    (Camera::NDK::Cam().ACameraMetadata_getConstEntry_ptr(metadata, tag, entry))
#define ACameraMetadata_free(metadata)                                                       \
    (Camera::NDK::Cam().ACameraMetadata_free_ptr(metadata))
#define ACameraDevice_close(device) (Camera::NDK::Cam().ACameraDevice_close_ptr(device))
#define ACameraDevice_createCaptureSession(device, outputs, callbacks, session)              \
    (Camera::NDK::Cam().ACameraDevice_createCaptureSession_ptr(device, outputs, callbacks,   \
                                                               session))
#define ACameraDevice_createCaptureRequest(device, templateId, request)                      \
    (Camera::NDK::Cam().ACameraDevice_createCaptureRequest_ptr(device, templateId, request))
#define ACaptureSessionOutputContainer_create(container)                                     \
    (Camera::NDK::Cam().ACaptureSessionOutputContainer_create_ptr(container))
#define ACaptureSessionOutputContainer_add(container, output)                                \
    (Camera::NDK::Cam().ACaptureSessionOutputContainer_add_ptr(container, output))
#define ACaptureSessionOutputContainer_free(container)                                       \
    (Camera::NDK::Cam().ACaptureSessionOutputContainer_free_ptr(container))
#define ACaptureSessionOutput_create(window, output)                                         \
    (Camera::NDK::Cam().ACaptureSessionOutput_create_ptr(window, output))
#define ACaptureSessionOutput_free(output)                                                   \
    (Camera::NDK::Cam().ACaptureSessionOutput_free_ptr(output))
#define ACameraOutputTarget_create(window, output)                                           \
    (Camera::NDK::Cam().ACameraOutputTarget_create_ptr(window, output))
#define ACameraOutputTarget_free(output)                                                     \
    (Camera::NDK::Cam().ACameraOutputTarget_free_ptr(output))
#define ACaptureRequest_free(request) (Camera::NDK::Cam().ACaptureRequest_free_ptr(request))
#define ACaptureRequest_addTarget(request, target)                                           \
    (Camera::NDK::Cam().ACaptureRequest_addTarget_ptr(request, target))
#define ACameraCaptureSession_close(session)                                                 \
    (Camera::NDK::Cam().ACameraCaptureSession_close_ptr(session))
#define ACameraCaptureSession_setRepeatingRequest(session, callbacks, numRequests, requests, \
                                                  frameTimestamp)                            \
    (Camera::NDK::Cam().ACameraCaptureSession_setRepeatingRequest_ptr(                       \
        session, callbacks, numRequests, requests, frameTimestamp))

#define AImageReader_new(width, height, format, maxImages, reader)                           \
    (Camera::NDK::Cam().AImageReader_new_ptr(width, height, format, maxImages, reader))
#define AImageReader_delete(reader) (Camera::NDK::Cam().AImageReader_delete_ptr(reader))
#define AImageReader_setImageListener(reader, listener)                                      \
    (Camera::NDK::Cam().AImageReader_setImageListener_ptr(reader, listener))
#define AImageReader_getWindow(reader, window)                                               \
    (Camera::NDK::Cam().AImageReader_getWindow_ptr(reader, window))
#define AImageReader_acquireLatestImage(reader, image)                                       \
    (Camera::NDK::Cam().AImageReader_acquireLatestImage_ptr(reader, image))
#define AImage_delete(image) (Camera::NDK::Cam().AImage_delete_ptr(image))
#define AImage_getPlaneData(image, plane, data, size)                                        \
    (Camera::NDK::Cam().AImage_getPlaneData_ptr(image, plane, data, size))
#define AImage_getPlaneRowStride(image, plane, stride)                                       \
    (Camera::NDK::Cam().AImage_getPlaneRowStride_ptr(image, plane, stride))
#define AImage_getPlanePixelStride(image, plane, stride)                                     \
    (Camera::NDK::Cam().AImage_getPlanePixelStride_ptr(image, plane, stride))
