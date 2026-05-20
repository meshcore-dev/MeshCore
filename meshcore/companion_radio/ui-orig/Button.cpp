#include "Button.h"

Button::Button(uint8_t pin, bool activeState) 
    : _pin(pin), _activeState(activeState), _isAnalog(false), _analogThreshold(20) {
    _currentState = false;  // Initialize as not pressed
    _lastState = _currentState;
}

Button::Button(uint8_t pin, bool activeState, bool isAnalog, uint16_t analogThreshold)
    : _pin(pin), _activeState(activeState), _isAnalog(isAnalog), _analogThreshold(analogThreshold) {
    _currentState = false;  // Initialize as not pressed
    _lastState = _currentState;
}

void Button::begin() {
    _currentState = readButton();
    _lastState = _currentState;
}

void Button::update() {
    uint32_t now = millis();
    
    // Read button at specified interval
    if (now - _lastReadTime < BUTTON_READ_INTERVAL_MS) {
        return;
    }
    _lastReadTime = now;
    
    bool newState = readButton();
    
    // Check if state has changed
    if (newState != _lastState) {
        _stateChangeTime = now;
    }
    
    // Debounce check
    if ((now - _stateChangeTime) > BUTTON_DEBOUNCE_TIME_MS) {
        if (newState != _currentState) {
            _currentState = newState;
            handleStateChange();
        }
    }
    
    _lastState = newState;
    
    // Handle multi-click timeout
    if (_state == WAITING_FOR_MULTI_CLICK && (now - _releaseTime) > BUTTON_CLICK_TIMEOUT_MS) {
        // Timeout reached, process the clicks
        if (_clickCount == 1) {
            triggerEvent(SHORT_PRESS);
        } else if (_clickCount == 2) {
            triggerEvent(DOUBLE_PRESS);
        } else if (_clickCount == 3) {
            triggerEvent(TRIPLE_PRESS);
        } else if (_clickCount >= 4) {
            triggerEvent(QUADRUPLE_PRESS);
        }

        _clickCount = 0;
        _state = IDLE;
    }
    
    // Handle long press while button is held
    if (_state == PRESSED && (now - _pressTime) > BUTTON_LONG_PRESS_TIME_MS) {
        triggerEvent(LONG_PRESS);
        _state = IDLE;  // Prevent multiple press events
        _clickCount = 0;
    }
}

bool Button::readButton() {
    if (_isAnalog) {
        return (analogRead(_pin) < _analogThreshold);
    } else {
        return (digitalRead(_pin) == _activeState);
    }
}

void Button::handleStateChange() {
    uint32_t now = millis();
    
    if (_currentState) {
        // Button pressed
        _pressTime = now;
        _state = PRESSED;
        triggerEvent(ANY_PRESS);
    } else {
        // Button released
        if (_state == PRESSED) {
            uint32_t pressDuration = now - _pressTime;
            
            if (pressDuration < BUTTON_LONG_PRESS_TIME_MS) {
                // Short press detected
                _clickCount++;
                _releaseTime = now;
                _state = WAITING_FOR_MULTI_CLICK;
            } else {
                // Long press already handled in update()
                _state = IDLE;
                _clickCount = 0;
            }
        }
    }
}

void Button::triggerEvent(EventType event) {
    _lastEvent = event;
    
    switch (event) {
        case ANY_PRESS:
            if (_onAnyPress) _onAnyPress();
            break;
        case SHORT_PRESS:
            if (_onShortPress) _onShortPress();
            break;
        case DOUBLE_PRESS:
            if (_onDoublePress) _onDoublePress();
            break;
        case TRIPLE_PRESS:
            if (_onTriplePress) _onTriplePress();
            break;
        case QUADRUPLE_PRESS:
            if (_onQuadruplePress) _onQuadruplePress();
            break;
        case LONG_PRESS:
            if (_onLongPress) _onLongPress();
            break;
        default:
            break;
    }
}