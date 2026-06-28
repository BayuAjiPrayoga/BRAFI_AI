#ifndef FACE_ENGINE_H
#define FACE_ENGINE_H

#include <Arduino.h>
#include <Adafruit_SSD1306.h>

enum class FaceState { Idle, Listening, Thinking, Speaking };
enum class FaceEmotion { Neutral, Happy, Sad, Angry, Surprised };

class FaceEngine {
public:
    FaceEngine(Adafruit_SSD1306* display);
    
    // Konfigurasi parameter wajah (bisa dari NVS/Web Dashboard)
    void setFaceParameters(int eye_w, int eye_h, int eye_spacing, int mouth_w, int mouth_h, int eye_y, int mouth_y);
    
    // Atur kondisi saat ini
    void setState(FaceState state);
    void setEmotion(FaceEmotion emotion);
    
    // Panggil fungsi ini berulang kali di loop() atau task
    void update(); 

private:
    void updateBlink();
    void setTargetsForIdle();
    void setTargetsForListening();
    void setTargetsForThinking();
    void setTargetsForSpeaking();
    void applySmooth();

    Adafruit_SSD1306* display_;

    FaceState state_ = FaceState::Idle;
    FaceEmotion emotion_ = FaceEmotion::Neutral;

    // Parameter dasar
    int base_eye_w_ = 12;
    int base_eye_h_ = 16;
    int eye_spacing_ = 16;
    int base_mouth_w_ = 14;
    int base_mouth_h_ = 3;
    int eye_y_ = -6;
    int mouth_y_ = 18;
    int corner_radius_ = 3;

    // Target pergerakan
    int tgt_left_w_ = 0, tgt_left_h_ = 0;
    int tgt_right_w_ = 0, tgt_right_h_ = 0;
    int tgt_mouth_w_ = 0, tgt_mouth_h_ = 0;
    int tgt_off_x_ = 0, tgt_off_y_ = 0;

    // Current (posisi saat ini, yang di-interpolate/lerp)
    float cur_left_w_ = 0, cur_left_h_ = 0;
    float cur_right_w_ = 0, cur_right_h_ = 0;
    float cur_mouth_w_ = 0, cur_mouth_h_ = 0;
    float cur_off_x_ = 0, cur_off_y_ = 0;

    // Timer state (menggunakan millis)
    int blink_phase_ = 0;
    unsigned long last_blink_time_ = 0;
    unsigned long next_blink_delay_ = 3000;
    
    unsigned long last_move_time_ = 0;
    unsigned long next_move_delay_ = 2000;
    
    unsigned long last_speak_time_ = 0;
    unsigned long next_speak_delay_ = 100;
    int speak_mouth_tgt_ = 4;
};

#endif // FACE_ENGINE_H
