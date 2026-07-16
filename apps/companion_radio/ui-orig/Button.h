#pragma once

#include <Arduino.h>
#include <functional>

// Button timing configuration
#define BUTTON_DEBOUNCE_TIME_MS    50      // Debounce time in ms
#define BUTTON_CLICK_TIMEOUT_MS    500     // Max time between clicks for multi-click
#define BUTTON_LONG_PRESS_TIME_MS  3000    // Time to trigger long press (3 seconds)
#define BUTTON_READ_INTERVAL_MS    10      // How often to read the button

class Button {
public:
    enum EventType {
        NONE,
        SHORT_PRESS,
        DOUBLE_PRESS,
        TRIPLE_PRESS,
        QUADRUPLE_PRESS,
        LONG_PRESS,
        ANY_PRESS
    };

    using EventCallback = std::function<void()>;

    Button(uint8_t pin, bool activeState = LOW);
    Button(uint8_t pin, bool activeState, bool isAnalog, uint16_t analogThreshold = 20);
    
    void begin();
    void update();
    
    // Set callbacks for different events
    void onShortPress(EventCallback callback) { _onShortPress = callback; }
    void onDoublePress(EventCallback callback) { _onDoublePress = callback; }
    void onTriplePress(EventCallback callback) { _onTriplePress = callback; }
    void onQuadruplePress(EventCallback callback) { _onQuadruplePress = callback; }
    void onLongPress(EventCallback callback) { _onLongPress = callback; }
    void onAnyPress(EventCallback callback) { _onAnyPress = callback; }
    
    // State getters
    bool isPressed() const { return _currentState; }
    EventType getLastEvent() const { return _lastEvent; }

private:
    enum State {
        IDLE,
        PRESSED,
        RELEASED,
        WAITING_FOR_MULTI_CLICK
    };

    uint8_t _pin;
    bool _activeState;
    bool _isAnalog;
    uint16_t _analogThreshold;
    
    State _state = IDLE;
    bool _currentState;
    bool _lastState;
    
    uint32_t _stateChangeTime = 0;
    uint32_t _pressTime = 0;
    uint32_t _releaseTime = 0;
    uint32_t _lastReadTime = 0;
    
    uint8_t _clickCount = 0;
    EventType _lastEvent = NONE;
    
    // Callbacks
    EventCallback _onShortPress = nullptr;
    EventCallback _onDoublePress = nullptr;
    EventCallback _onTriplePress = nullptr;
    EventCallback _onQuadruplePress = nullptr;
    EventCallback _onLongPress = nullptr;
    EventCallback _onAnyPress = nullptr;
    
    bool readButton();
    void handleStateChange();
    void triggerEvent(EventType event);
};