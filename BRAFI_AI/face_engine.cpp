#include "face_engine.h"
#include "display.h"
#include <math.h>

#define CENTER_X 64
#define CENTER_Y 32

// Interpolation (lerp)
static float smoothLerp(float current, float target, float speed) {
    float diff = target - current;
    if (abs(diff) < 0.5) return target; // Snap
    return current + (diff * speed);
}

FaceEngine::FaceEngine(Adafruit_SSD1306* display) : display_(display) {
    // Set initial current values to base values to avoid jump
    cur_left_w_ = tgt_left_w_ = base_eye_w_;
    cur_left_h_ = tgt_left_h_ = base_eye_h_;
    cur_right_w_ = tgt_right_w_ = base_eye_w_;
    cur_right_h_ = tgt_right_h_ = base_eye_h_;
    cur_mouth_w_ = tgt_mouth_w_ = base_mouth_w_;
    cur_mouth_h_ = tgt_mouth_h_ = base_mouth_h_;
    
    last_blink_time_ = millis();
    last_move_time_ = millis();
}

void FaceEngine::setFaceParameters(int eye_w, int eye_h, int eye_spacing, int mouth_w, int mouth_h, int eye_y, int mouth_y) {
    base_eye_w_ = eye_w;
    base_eye_h_ = eye_h;
    eye_spacing_ = eye_spacing;
    base_mouth_w_ = mouth_w;
    base_mouth_h_ = mouth_h;
    eye_y_ = eye_y;
    mouth_y_ = mouth_y;
    
    // Reset targets instantly
    tgt_left_w_ = base_eye_w_;
    tgt_left_h_ = base_eye_h_;
    tgt_right_w_ = base_eye_w_;
    tgt_right_h_ = base_eye_h_;
    tgt_mouth_w_ = base_mouth_w_;
    tgt_mouth_h_ = base_mouth_h_;
}

void FaceEngine::setState(FaceState state) {
    if (state_ != state) {
        if (state == FaceState::Speaking) {
            last_speak_time_ = millis();
        }
        state_ = state;
    }
}

void FaceEngine::setEmotion(FaceEmotion emotion) {
    emotion_ = emotion;
}

void FaceEngine::updateBlink() {
    unsigned long now = millis();
    
    if (blink_phase_ == 0) {
        if (now - last_blink_time_ > next_blink_delay_) {
            blink_phase_ = 1;
            last_blink_time_ = now;
            // Next blink in 2-6 seconds
            next_blink_delay_ = random(2000, 6000);
        }
    } else {
        // Blink sequence (approx 150ms total)
        unsigned long elapsed = now - last_blink_time_;
        if (elapsed < 30) {
            blink_phase_ = 1; // Closing
        } else if (elapsed < 60) {
            blink_phase_ = 2; // Closed
        } else if (elapsed < 120) {
            blink_phase_ = 3; // Opening
        } else {
            blink_phase_ = 0; // Done
        }
    }
}

void FaceEngine::setTargetsForIdle() {
    unsigned long now = millis();
    
    // Random pergerakan bola mata halus
    if (emotion_ == FaceEmotion::Neutral && (now - last_move_time_ > next_move_delay_)) {
        if (random(100) < 40) { // 40% chance untuk melirik
            tgt_off_x_ = random(-5, 6); // -5 to +5
            tgt_off_y_ = random(-3, 4); // -3 to +3
            next_move_delay_ = random(800, 2500); 
        } else {
            tgt_off_x_ = 0; // kembali menatap lurus
            tgt_off_y_ = 0;
            next_move_delay_ = random(1500, 4000);
        }
        last_move_time_ = now;
    }

    int ew = base_eye_w_;
    int eh = base_eye_h_;
    int mw = base_mouth_w_;
    int mh = base_mouth_h_;

    tgt_left_w_ = ew;
    tgt_left_h_ = eh;
    tgt_right_w_ = ew;
    tgt_right_h_ = eh;
    tgt_mouth_w_ = mw;
    tgt_mouth_h_ = mh;
}

void FaceEngine::setTargetsForListening() {
    unsigned long now = millis();
    
    // Animasi "bernapas" lambat (organic pulse)
    float pulse = sin(now / 300.0) * 1.5; 
    
    // Mata sedikit membesar, fokus dan attentif
    tgt_left_w_ = base_eye_w_ + 2 + pulse;
    tgt_left_h_ = base_eye_h_ + 2 + pulse;
    tgt_right_w_ = base_eye_w_ + 2 + pulse;
    tgt_right_h_ = base_eye_h_ + 2 + pulse;

    // Menatap fokus ke depan
    tgt_off_x_ = 0;
    tgt_off_y_ = 0;

    // Mulut membentuk huruf 'O' kecil seolah sedang menyimak ("Ooo..")
    tgt_mouth_w_ = 6 + pulse;
    tgt_mouth_h_ = 6 + pulse;
}

void FaceEngine::setTargetsForThinking() {
    unsigned long now = millis();
    
    // Ekspresi Berpikir: Satu mata menyipit (squint), satu mata membesar
    tgt_left_w_ = base_eye_w_;
    tgt_left_h_ = base_eye_h_ / 2.5;  // Menyipit
    tgt_right_w_ = base_eye_w_ + 2;
    tgt_right_h_ = base_eye_h_ + 2;   // Membesar

    // Melirik ke atas (mencari ingatan)
    tgt_off_y_ = -6;

    // Mata bergerak-gerak kecil ke kiri dan kanan (Processing)
    int phase = (now / 350) % 2;
    if (phase == 0) tgt_off_x_ = 3;
    else tgt_off_x_ = 6;

    // Mulut menyusut dan bergeser seolah sedang "hmmm..."
    tgt_mouth_w_ = 6;
    tgt_mouth_h_ = 3;
}

void FaceEngine::setTargetsForSpeaking() {
    // Mata sedikit rileks / santai
    tgt_left_w_ = base_eye_w_;
    tgt_left_h_ = base_eye_h_ - 2;
    tgt_right_w_ = base_eye_w_;
    tgt_right_h_ = base_eye_h_ - 2;

    // Sesekali melirik kecil saat bicara
    unsigned long now = millis();
    if (now - last_move_time_ > 1500) {
        tgt_off_x_ = random(-2, 3);
        tgt_off_y_ = random(-1, 2);
        last_move_time_ = now;
    }

    // Animasi mulut bicara yang dinamis
    if (now - last_speak_time_ > next_speak_delay_) {
        last_speak_time_ = now;
        next_speak_delay_ = random(70, 160); // Kecepatan suku kata
        
        int r = random(100);
        if (r < 20) {
            speak_mouth_tgt_ = 2; // Hampir tertutup
            tgt_mouth_w_ = base_mouth_w_;
        }
        else if (r < 60) {
            speak_mouth_tgt_ = 8; // Terbuka sedang
            tgt_mouth_w_ = base_mouth_w_ - 2;
        }
        else {
            speak_mouth_tgt_ = 14; // Terbuka lebar (A, O)
            tgt_mouth_w_ = base_mouth_w_ - 4; // Makin tinggi mulut, makin sempit lebarnya
        }
    }
    tgt_mouth_h_ = speak_mouth_tgt_;
}

void FaceEngine::applySmooth() {
    float speed = 0.3; // Lerp speed (higher = faster)
    float mouth_speed = (state_ == FaceState::Speaking) ? 0.6 : 0.3;

    cur_left_w_ = smoothLerp(cur_left_w_, tgt_left_w_, speed);
    cur_left_h_ = smoothLerp(cur_left_h_, tgt_left_h_, speed);
    cur_right_w_ = smoothLerp(cur_right_w_, tgt_right_w_, speed);
    cur_right_h_ = smoothLerp(cur_right_h_, tgt_right_h_, speed);
    
    cur_mouth_w_ = smoothLerp(cur_mouth_w_, tgt_mouth_w_, mouth_speed);
    cur_mouth_h_ = smoothLerp(cur_mouth_h_, tgt_mouth_h_, mouth_speed);
    
    cur_off_x_ = smoothLerp(cur_off_x_, tgt_off_x_, speed);
    cur_off_y_ = smoothLerp(cur_off_y_, tgt_off_y_, speed);
}

void FaceEngine::update() {
    if (!display_) return;

    if (state_ == FaceState::Idle) setTargetsForIdle();
    else if (state_ == FaceState::Listening) setTargetsForListening();
    else if (state_ == FaceState::Thinking) setTargetsForThinking();
    else if (state_ == FaceState::Speaking) setTargetsForSpeaking();

    updateBlink();

    // Override eye height if blinking
    float render_left_h = tgt_left_h_;
    float render_right_h = tgt_right_h_;
    
    if (blink_phase_ > 0) {
        if (blink_phase_ == 1 || blink_phase_ == 3) {
            render_left_h = tgt_left_h_ * 0.5;
            render_right_h = tgt_right_h_ * 0.5;
        } else if (blink_phase_ == 2) {
            render_left_h = 2;
            render_right_h = 2;
        }
    }

    // Force targets so lerp follows blink quickly
    if (blink_phase_ > 0) {
        tgt_left_h_ = render_left_h;
        tgt_right_h_ = render_right_h;
    }

    applySmooth();

    if (displayMutex != NULL) {
        if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Clear display buffer
            display_->clearDisplay();

            // Calculate drawing coordinates (centered)
            int lx = CENTER_X - eye_spacing_ + (int)cur_off_x_ - (int)(cur_left_w_/2);
            int ly = CENTER_Y + eye_y_ + (int)cur_off_y_ - (int)(cur_left_h_/2);
            
            int rx = CENTER_X + eye_spacing_ + (int)cur_off_x_ - (int)(cur_right_w_/2);
            int ry = CENTER_Y + eye_y_ + (int)cur_off_y_ - (int)(cur_right_h_/2);
            
            int mx = CENTER_X + (int)cur_off_x_ - (int)(cur_mouth_w_/2);
            int my = CENTER_Y + mouth_y_ + (int)cur_off_y_ - (int)(cur_mouth_h_/2);

            // Draw elements
            display_->fillRoundRect(lx, ly, (int)cur_left_w_, (int)cur_left_h_, corner_radius_, SSD1306_WHITE);
            display_->fillRoundRect(rx, ry, (int)cur_right_w_, (int)cur_right_h_, corner_radius_, SSD1306_WHITE);
            display_->fillRoundRect(mx, my, (int)cur_mouth_w_, (int)cur_mouth_h_, corner_radius_, SSD1306_WHITE);

            // Send to screen
            display_->display();
            
            xSemaphoreGive(displayMutex);
        }
    }
}
