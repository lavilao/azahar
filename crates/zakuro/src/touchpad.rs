//! the controls drawn over the game and touched directly. egui keeps track
//! of only one pointer, but a game wants a button and the circle pad held
//! at once, so the raw winit touches are followed here, each by its id.
//!
//! the geometry is in physical pixels, worked out from the window's size, so
//! that it comes out in both orientations: the pad in the bottom left, the
//! buttons in the bottom right, L and R in the top corners.

// only Android drives this; the desktop has a keyboard and controllers
#![cfg_attr(not(target_os = "android"), allow(dead_code))]

use std::collections::HashMap;

use egui::{Color32, FontId, Painter, Pos2, Stroke, Vec2};
use winit::event::TouchPhase;
use zakuro_core::services::hid::{InputState, PadState};

/// how far the circle pad has to be pushed before it counts.
const DEADZONE: f32 = 0.2;

/// what one finger is holding.
#[derive(Clone, Copy, PartialEq)]
enum Held {
    Button(PadState),
    /// the circle pad, from where the touch began.
    Circle,
}

pub struct Touchpad {
    /// one entry per finger on a control.
    touches: HashMap<u64, Held>,
    /// the circle pad's position, -1 to 1 each way, none without a touch.
    stick: Option<[f32; 2]>,
}

impl Touchpad {
    pub fn new() -> Touchpad {
        Touchpad { touches: HashMap::new(), stick: None }
    }

    /// lets go of everything, when the game stops seeing input.
    pub fn release(&mut self) {
        self.touches.clear();
        self.stick = None;
    }

    /// a touch as winit reports it, in window pixels. true when a control
    /// took it, in which case egui does not see it too.
    pub fn touch(&mut self, phase: TouchPhase, id: u64, position: (f32, f32), size: (u32, u32)) -> bool {
        let layout = Layout::new(size);
        match phase {
            TouchPhase::Started => {
                let held = layout.control_at(position);
                if let Some(held) = held {
                    self.touches.insert(id, held);
                    if held == Held::Circle {
                        self.stick = layout.circle_from(position);
                    }
                }
                held.is_some()
            }
            TouchPhase::Moved => match self.touches.get(&id) {
                Some(Held::Circle) => {
                    self.stick = layout.circle_from(position);
                    true
                }
                Some(_) => true,
                None => false,
            },
            TouchPhase::Ended | TouchPhase::Cancelled => {
                let had = self.touches.remove(&id);
                if had == Some(Held::Circle) {
                    self.stick = None;
                }
                had.is_some()
            }
        }
    }

    /// what the fingers hold, over what the keyboard does.
    pub fn apply(&self, mut state: InputState) -> InputState {
        for held in self.touches.values() {
            if let Held::Button(button) = held {
                state.buttons.set(*button, true);
            }
        }
        if let Some([x, y]) = self.stick {
            if x.abs().max(y.abs()) > DEADZONE {
                state.circle_x = x;
                state.circle_y = y;
            }
        }
        state
    }

    /// the controls, over whatever else is on the window.
    pub fn draw(&self, painter: &Painter, pixels_per_point: f32) {
        let scale = 1.0 / pixels_per_point;
        let size = (
            painter.clip_rect().width() * pixels_per_point,
            painter.clip_rect().height() * pixels_per_point,
        );
        let layout = Layout::new((size.0.max(1.0) as u32, size.1.max(1.0) as u32));
        let pressed = |button: PadState| self.touches.values().any(|held| *held == Held::Button(button));
        let circle_held = self.touches.values().any(|held| *held == Held::Circle);

        let circle = |center: Pos2, radius: f32, on: bool, label: &str, label_scale: f32| {
            let center = Pos2::new(center.x * scale, center.y * scale);
            let radius = radius * scale;
            let fill = if on { pressed_color() } else { rest_color() };
            painter.circle(center, radius, fill, Stroke::new(2.0 * scale, Color32::WHITE));
            painter.text(
                center,
                egui::Align2::CENTER_CENTER,
                label,
                FontId::proportional(radius * label_scale),
                Color32::WHITE,
            );
        };

        for (button, place, label) in [
            (PadState::A, layout.a, "A"),
            (PadState::B, layout.b, "B"),
            (PadState::X, layout.x, "X"),
            (PadState::Y, layout.y, "Y"),
        ] {
            circle(place, layout.face_radius, pressed(button), label, 0.9);
        }
        circle(layout.l, layout.corner_radius, pressed(PadState::L), "L", 0.9);
        circle(layout.r, layout.corner_radius, pressed(PadState::R), "R", 0.9);
        circle(layout.start, layout.small_radius, pressed(PadState::START), "Start", 0.55);
        circle(layout.select, layout.small_radius, pressed(PadState::SELECT), "Select", 0.55);

        // the circle pad, with where the finger holds it shown
        let center = Pos2::new(layout.circle.x * scale, layout.circle.y * scale);
        let radius = layout.circle_radius * scale;
        painter.circle(center, radius, if circle_held { pressed_color() } else { rest_color() }, Stroke::new(2.0 * scale, Color32::WHITE));
        if let Some([x, y]) = self.stick {
            let knob = center + Vec2::new(x * radius * 0.6, -y * radius * 0.6);
            painter.circle(knob, radius * 0.35, Color32::from_white_alpha(200), Stroke::new(1.0 * scale, Color32::BLACK));
        }
    }
}

fn rest_color() -> Color32 {
    Color32::from_rgba_unmultiplied(60, 60, 70, 110)
}

fn pressed_color() -> Color32 {
    Color32::from_rgba_unmultiplied(120, 190, 255, 170)
}

/// where the controls are, in physical pixels.
struct Layout {
    circle: Pos2,
    circle_radius: f32,
    a: Pos2,
    b: Pos2,
    x: Pos2,
    y: Pos2,
    face_radius: f32,
    l: Pos2,
    r: Pos2,
    corner_radius: f32,
    start: Pos2,
    select: Pos2,
    small_radius: f32,
}

impl Layout {
    fn new(size: (u32, u32)) -> Layout {
        let (w, h) = (size.0 as f32, size.1 as f32);
        let small = w.min(h);
        // the same on a phone in either orientation
        let circle_radius = 0.115 * small;
        let face_radius = 0.055 * small;
        let corner_radius = 0.045 * small;
        let small_radius = 0.035 * small;
        let face = 0.065 * small;
        Layout {
            circle: Pos2::new(0.21 * w, h - 0.17 * h),
            circle_radius,
            // the 3DS has X above, Y left, A right, B below
            a: Pos2::new(0.79 * w + face, h - 0.21 * h),
            b: Pos2::new(0.79 * w, h - 0.21 * h + face),
            x: Pos2::new(0.79 * w, h - 0.21 * h - face),
            y: Pos2::new(0.79 * w - face, h - 0.21 * h),
            face_radius,
            l: Pos2::new(0.10 * w, 0.07 * h),
            r: Pos2::new(0.90 * w, 0.07 * h),
            corner_radius,
            start: Pos2::new(0.50 * w + 0.06 * small, h - 0.055 * h),
            select: Pos2::new(0.50 * w - 0.06 * small, h - 0.055 * h),
            small_radius,
        }
    }

    /// what a touch that begins here lands on.
    fn control_at(&self, position: (f32, f32)) -> Option<Held> {
        let p = Pos2::new(position.0, position.1);
        let in_circle = |center: Pos2, radius: f32| p.distance(center) < radius * 1.4;
        let button = |center: Pos2, radius: f32, pad: PadState| in_circle(center, radius).then_some(Held::Button(pad));
        // the face buttons' diamonds overlap a little; nearest wins
        let faces = [
            (self.x, PadState::X),
            (self.y, PadState::Y),
            (self.a, PadState::A),
            (self.b, PadState::B),
        ];
        if faces.iter().any(|&(center, _)| in_circle(center, self.face_radius * 1.6)) {
            let (_, nearest) = faces.iter().copied().min_by_key(|&(center, _)| p.distance(center) as u32).unwrap();
            return Some(Held::Button(nearest));
        }
        button(self.l, self.corner_radius, PadState::L)
            .or_else(|| button(self.r, self.corner_radius, PadState::R))
            .or_else(|| button(self.start, self.small_radius, PadState::START))
            .or_else(|| button(self.select, self.small_radius, PadState::SELECT))
            .or_else(|| in_circle(self.circle, self.circle_radius * 1.2).then_some(Held::Circle))
    }

    /// where the circle pad is held, from -1 to 1 each way.
    fn circle_from(&self, position: (f32, f32)) -> Option<[f32; 2]> {
        let dx = (position.0 - self.circle.x) / self.circle_radius;
        let dy = (self.circle.y - position.1) / self.circle_radius;
        let length = dx.hypot(dy);
        if length > 1.0 {
            Some([dx / length, dy / length])
        } else {
            Some([dx, dy])
        }
    }
}
