#include "PsychicMqttClient.h"

#include <string.h>

static const char *TAG = "🐙";

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

PsychicMqttClient::PsychicMqttClient() : _mqtt_cfg()
{
    memset(&_mqtt_cfg, 0, sizeof(_mqtt_cfg));
    _topic[0] = '\0';
}

PsychicMqttClient::~PsychicMqttClient()
{
    disconnect();
    if (_client != nullptr)
    {
        esp_mqtt_client_destroy(_client);
        _client = nullptr;
    }

    if (_buffer != nullptr)
    {
        free(_buffer);
        _buffer = nullptr;
        _buffer_capacity = 0;
    }
    // Topic storage is inline; nothing to free.
    // Subscription entries have inline topic storage; nothing to free.
}

PsychicMqttClient &PsychicMqttClient::setKeepAlive(int keepAlive)
{
#if ESP_IDF_VERSION_MAJOR == 5
    _mqtt_cfg.session.keepalive = keepAlive;
#else
    _mqtt_cfg.keepalive = keepAlive;
#endif
    _config_dirty = true;
    return *this;
}

PsychicMqttClient &PsychicMqttClient::setNetworkTimeout(int timeoutMs)
{
#if ESP_IDF_VERSION_MAJOR == 5
    _mqtt_cfg.network.timeout_ms = timeoutMs;
#else
    _mqtt_cfg.network_timeout_ms = timeoutMs;
#endif
    _config_dirty = true;
    return *this;
}

PsychicMqttClient &PsychicMqttClient::setAutoReconnect(bool reconnect)
{
#if ESP_IDF_VERSION_MAJOR == 5
    _mqtt_cfg.network.disable_auto_reconnect = !reconnect;
#else
    _mqtt_cfg.disable_auto_reconnect = !reconnect;
#endif
    _config_dirty = true;
    return *this;
}

PsychicMqttClient &PsychicMqttClient::setMessageRetransmitTimeout(int timeoutMs)
{
#if ESP_IDF_VERSION_MAJOR == 5
    _mqtt_cfg.session.message_retransmit_timeout = timeoutMs;
#else
    _mqtt_cfg.message_retransmit_timeout = timeoutMs;
#endif
    _config_dirty = true;
    return *this;
}

PsychicMqttClient &PsychicMqttClient::setClientId(const char *clientId)
{
#if ESP_IDF_VERSION_MAJOR == 5
    _mqtt_cfg.credentials.client_id = clientId;
#else
    _mqtt_cfg.client_id = clientId;
#endif
    _config_dirty = true;
    return *this;
}

PsychicMqttClient &PsychicMqttClient::setCleanSession(bool cleanSession)
{
#if ESP_IDF_VERSION_MAJOR == 5
    _mqtt_cfg.session.disable_clean_session = !cleanSession;
#else
    _mqtt_cfg.disable_clean_session = !cleanSession;
#endif
    _config_dirty = true;
    return *this;
}

PsychicMqttClient &PsychicMqttClient::setBufferSize(int bufferSize)
{
#if ESP_IDF_VERSION_MAJOR == 5
    _mqtt_cfg.buffer.size = bufferSize;
#else
    _mqtt_cfg.buffer_size = bufferSize;
#endif
    _config_dirty = true;
    return *this;
}

PsychicMqttClient &PsychicMqttClient::setTaskStackAndPriority(int stackSize, int priority)
{
#if ESP_IDF_VERSION_MAJOR == 5
    _mqtt_cfg.task.stack_size = stackSize;
    _mqtt_cfg.task.priority = priority;
#else
    _mqtt_cfg.task_stack = stackSize;
    _mqtt_cfg.task_prio = priority;
#endif
    _config_dirty = true;
    return *this;
}

PsychicMqttClient &PsychicMqttClient::setCACert(const char *rootCA, size_t rootCALen)
{
#if ESP_IDF_VERSION_MAJOR == 5
    _mqtt_cfg.broker.verification.certificate = rootCA;
    _mqtt_cfg.broker.verification.certificate_len = rootCALen;
#else
    _mqtt_cfg.cert_pem = rootCA;
    _mqtt_cfg.cert_len = rootCALen;
#endif
    _config_dirty = true;
    return *this;
}

PsychicMqttClient &PsychicMqttClient::setCACertBundle(const uint8_t *bundle, size_t bundleLen)
{
#if ESP_IDF_VERSION_MAJOR == 5
    if (bundle != nullptr)
    {
        esp_crt_bundle_set(bundle, bundleLen);
        _mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    }
    else
    {
        esp_crt_bundle_detach(NULL);
        _mqtt_cfg.broker.verification.crt_bundle_attach = NULL;
    }
#else
    if (bundle != nullptr)
    {
        arduino_esp_crt_bundle_set(bundle);
        _mqtt_cfg.crt_bundle_attach = arduino_esp_crt_bundle_attach;
    }
    else
    {
        arduino_esp_crt_bundle_detach(NULL);
        _mqtt_cfg.crt_bundle_attach = NULL;
    }
#endif
    _config_dirty = true;
    return *this;
}

PsychicMqttClient &PsychicMqttClient::attachArduinoCACertBundle(bool attach)
{
#if ESP_IDF_VERSION_MAJOR == 5
    if (attach)
        _mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    else
        _mqtt_cfg.broker.verification.crt_bundle_attach = NULL;
#else
    if (attach)
        _mqtt_cfg.crt_bundle_attach = arduino_esp_crt_bundle_attach;
    else
        _mqtt_cfg.crt_bundle_attach = NULL;
#endif
    _config_dirty = true;
    return *this;
}

PsychicMqttClient &PsychicMqttClient::setCredentials(const char *username, const char *password)
{
#if ESP_IDF_VERSION_MAJOR == 5
    _mqtt_cfg.credentials.username = username;
    if (password != nullptr)
        _mqtt_cfg.credentials.authentication.password = password;
#else
    _mqtt_cfg.username = username;
    if (password != nullptr)
        _mqtt_cfg.password = password;
#endif
    _config_dirty = true;
    return *this;
}

PsychicMqttClient &PsychicMqttClient::setClientCertificate(const char *clientCert, const char *clientKey,
                                                           size_t clientCertLen, size_t clientKeyLen)
{
#if ESP_IDF_VERSION_MAJOR == 5
    _mqtt_cfg.credentials.authentication.certificate = clientCert;
    _mqtt_cfg.credentials.authentication.key = clientKey;
    _mqtt_cfg.credentials.authentication.certificate_len = clientCertLen;
    _mqtt_cfg.credentials.authentication.key_len = clientKeyLen;
#else
    _mqtt_cfg.client_cert_pem = clientCert;
    _mqtt_cfg.client_key_pem = clientKey;
    _mqtt_cfg.client_cert_len = clientCertLen;
    _mqtt_cfg.client_key_len = clientKeyLen;
#endif
    _config_dirty = true;
    return *this;
}

PsychicMqttClient &PsychicMqttClient::setWill(const char *topic, uint8_t qos, bool retain, const char *payload, int length)
{
#if ESP_IDF_VERSION_MAJOR == 5
    _mqtt_cfg.session.last_will.topic = topic;
    _mqtt_cfg.session.last_will.qos = qos;
    _mqtt_cfg.session.last_will.retain = retain;
    _mqtt_cfg.session.last_will.msg_len = length;
    _mqtt_cfg.session.last_will.msg = payload;
#else
    _mqtt_cfg.lwt_topic = topic;
    _mqtt_cfg.lwt_qos = qos;
    _mqtt_cfg.lwt_retain = retain;
    _mqtt_cfg.lwt_msg_len = length;
    _mqtt_cfg.lwt_msg = payload;
#endif
    _config_dirty = true;
    return *this;
}

PsychicMqttClient &PsychicMqttClient::setServer(const char *uri)
{
#if ESP_IDF_VERSION_MAJOR == 5
    _mqtt_cfg.broker.address.uri = uri;
#else
    _mqtt_cfg.uri = uri;
#endif
    _config_dirty = true;
    return *this;
}

PsychicMqttClient &PsychicMqttClient::onConnect(OnConnectUserCallback callback)
{
    if (_onConnectUserCallbackCount < PSYCHIC_MAX_CONNECT_CB)
    {
        _onConnectUserCallbacks[_onConnectUserCallbackCount++] = std::move(callback);
    }
    else
    {
        ESP_LOGE(TAG, "onConnect callback list full (max=%d)", PSYCHIC_MAX_CONNECT_CB);
    }
    return *this;
}

PsychicMqttClient &PsychicMqttClient::onDisconnect(OnDisconnectUserCallback callback)
{
    if (_onDisconnectUserCallbackCount < PSYCHIC_MAX_DISCONNECT_CB)
    {
        _onDisconnectUserCallbacks[_onDisconnectUserCallbackCount++] = std::move(callback);
    }
    else
    {
        ESP_LOGE(TAG, "onDisconnect callback list full (max=%d)", PSYCHIC_MAX_DISCONNECT_CB);
    }
    return *this;
}

PsychicMqttClient &PsychicMqttClient::onSubscribe(OnSubscribeUserCallback callback)
{
    if (_onSubscribeUserCallbackCount < PSYCHIC_MAX_SUBSCRIBE_CB)
    {
        _onSubscribeUserCallbacks[_onSubscribeUserCallbackCount++] = std::move(callback);
    }
    else
    {
        ESP_LOGE(TAG, "onSubscribe callback list full (max=%d)", PSYCHIC_MAX_SUBSCRIBE_CB);
    }
    return *this;
}

PsychicMqttClient &PsychicMqttClient::onUnsubscribe(OnUnsubscribeUserCallback callback)
{
    if (_onUnsubscribeUserCallbackCount < PSYCHIC_MAX_UNSUBSCRIBE_CB)
    {
        _onUnsubscribeUserCallbacks[_onUnsubscribeUserCallbackCount++] = std::move(callback);
    }
    else
    {
        ESP_LOGE(TAG, "onUnsubscribe callback list full (max=%d)", PSYCHIC_MAX_UNSUBSCRIBE_CB);
    }
    return *this;
}

PsychicMqttClient &PsychicMqttClient::onMessage(OnMessageUserCallback callback)
{
    if (_onMessageUserCallbackCount < PSYCHIC_MAX_MESSAGE_CB)
    {
        OnMessageUserCallback_t &slot = _onMessageUserCallbacks[_onMessageUserCallbackCount++];
        slot.topic[0] = '\0';
        slot.qos = 0;
        slot.callback = std::move(callback);
        slot.has_topic = false;
        slot.used = true;
    }
    else
    {
        ESP_LOGE(TAG, "onMessage callback list full (max=%d)", PSYCHIC_MAX_MESSAGE_CB);
    }
    return *this;
}

PsychicMqttClient &PsychicMqttClient::onTopic(const char *topic, int qos, OnMessageUserCallback callback)
{
    if (_onMessageUserCallbackCount >= PSYCHIC_MAX_MESSAGE_CB)
    {
        ESP_LOGE(TAG, "onTopic subscription list full (max=%d)", PSYCHIC_MAX_MESSAGE_CB);
        return *this;
    }
    OnMessageUserCallback_t &slot = _onMessageUserCallbacks[_onMessageUserCallbackCount++];
    size_t tlen = strnlen(topic, PSYCHIC_MAX_TOPIC_LEN - 1);
    memcpy(slot.topic, topic, tlen);
    slot.topic[tlen] = '\0';
    slot.qos = qos;
    slot.callback = std::move(callback);
    slot.has_topic = true;
    slot.used = true;

    if (_connected)
        subscribe(slot.topic, qos);
    return *this;
}

PsychicMqttClient &PsychicMqttClient::onPublish(OnPublishUserCallback callback)
{
    if (_onPublishUserCallbackCount < PSYCHIC_MAX_PUBLISH_CB)
    {
        _onPublishUserCallbacks[_onPublishUserCallbackCount++] = std::move(callback);
    }
    else
    {
        ESP_LOGE(TAG, "onPublish callback list full (max=%d)", PSYCHIC_MAX_PUBLISH_CB);
    }
    return *this;
}

PsychicMqttClient &PsychicMqttClient::onError(OnErrorUserCallback callback)
{
    if (_onErrorUserCallbackCount < PSYCHIC_MAX_ERROR_CB)
    {
        _onErrorUserCallbacks[_onErrorUserCallbackCount++] = std::move(callback);
    }
    else
    {
        ESP_LOGE(TAG, "onError callback list full (max=%d)", PSYCHIC_MAX_ERROR_CB);
    }
    return *this;
}

bool PsychicMqttClient::connected()
{
    return _connected;
}

void PsychicMqttClient::connect()
{
#if ESP_IDF_VERSION_MAJOR == 5
    if (_mqtt_cfg.broker.address.uri == nullptr)
    {
        ESP_LOGE(TAG, "MQTT URI not set.");
        return;
    }
    int desired_buffer = _mqtt_cfg.buffer.size > 0 ? _mqtt_cfg.buffer.size : 1024;
#else
    if (_mqtt_cfg.uri == nullptr)
    {
        ESP_LOGE(TAG, "MQTT URI not set.");
        return;
    }
    int desired_buffer = _mqtt_cfg.buffer_size > 0 ? _mqtt_cfg.buffer_size : 1024;
#endif

    // Lazily size the reassembly buffer once for the client's lifetime.
    // Messages larger than this will be rejected rather than triggering
    // per-message heap allocation.
    if (_buffer == nullptr)
    {
        _buffer_capacity = (size_t)desired_buffer;
        _buffer = (char *)malloc(_buffer_capacity + 1);
        if (_buffer == nullptr)
        {
            ESP_LOGE(TAG, "Failed to allocate reassembly buffer (%u bytes)", (unsigned)_buffer_capacity);
            _buffer_capacity = 0;
        }
    }

    if (_client == nullptr)
    {
        _client = esp_mqtt_client_init(&_mqtt_cfg);
        // Register event handler only once when client is first created
        // to avoid memory leak from repeated registrations
        esp_mqtt_client_register_event(_client, MQTT_EVENT_ANY, _onMqttEventStatic, this);
        _config_dirty = false;
    }
    else
    {
        if (_config_dirty)
        {
            esp_err_t cfg_result = esp_mqtt_set_config(_client, &_mqtt_cfg);
            ESP_ERROR_CHECK_WITHOUT_ABORT(cfg_result);
            if (cfg_result == ESP_OK)
            {
                _config_dirty = false;
                ESP_LOGD(TAG, "connect(): applied mqtt config update");
            }
            else
            {
                ESP_LOGW(TAG, "connect(): failed to apply mqtt config, will retry");
            }
        }
        else
        {
            ESP_LOGD(TAG, "connect(): mqtt config unchanged, skipping set_config");
        }
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_start(_client));
    ESP_LOGI(TAG, "MQTT client started.");
}

void PsychicMqttClient::reconnect()
{
    if (_client == nullptr)
    {
        ESP_LOGW(TAG, "MQTT client not initialized, cannot reconnect.");
        return;
    }
    if (_config_dirty)
    {
        // Apply config only when mutating setters changed _mqtt_cfg.
        esp_err_t cfg_result = esp_mqtt_set_config(_client, &_mqtt_cfg);
        ESP_ERROR_CHECK_WITHOUT_ABORT(cfg_result);
        if (cfg_result == ESP_OK)
        {
            _config_dirty = false;
            ESP_LOGD(TAG, "reconnect(): applied mqtt config update");
        }
        else
        {
            ESP_LOGW(TAG, "reconnect(): failed to apply mqtt config, reconnecting with previous config");
        }
    }
    else
    {
        ESP_LOGD(TAG, "reconnect(): mqtt config unchanged, skipping set_config");
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_reconnect(_client));
    ESP_LOGI(TAG, "MQTT client reconnect requested.");
}

void PsychicMqttClient::disconnect()
{
    if (_client == nullptr)
    {
        ESP_LOGW(TAG, "MQTT client not started.");
        return;
    }

    if (_connected)
    {
        ESP_LOGI(TAG, "Disconnecting MQTT client.");
        _stopMqttClient = false;
        esp_mqtt_client_disconnect(_client);

        // Wait for all disconnect events to be processed
        while (!_stopMqttClient)
        {
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }

    esp_mqtt_client_stop(_client);
    ESP_LOGI(TAG, "MQTT client stopped.");
}

void PsychicMqttClient::forceStop()
{
    if (_client == nullptr)
    {
        ESP_LOGW(TAG, "MQTT client not started.");
        return;
    }

    if (_connected)
    {
        ESP_LOGI(TAG, "Forced stop MQTT client.");
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_stop(_client));
    _connected = false;
    ESP_LOGI(TAG, "MQTT client forcefully stopped.");
}

int PsychicMqttClient::subscribe(const char *topic, int qos)
{
    if (_connected)
    {
        ESP_LOGI(TAG, "Subscribing to topic %s with QoS %d", topic, qos);
        return esp_mqtt_client_subscribe(_client, topic, qos);
    }
    else
    {
        ESP_LOGW(TAG, "MQTT client not connected. Dropping subscription to topic %s with QoS %d.", topic, qos);
        return -1;
    }
}

int PsychicMqttClient::unsubscribe(const char *topic)
{
    ESP_LOGI(TAG, "Unsubscribing from topic %s", topic);
    return esp_mqtt_client_unsubscribe(_client, topic);
}

int PsychicMqttClient::publish(const char *topic, int qos, bool retain, const char *payload, int length, bool async)
{
    // drop message if not connected and QoS is 0
    if (!connected() && qos == 0)
    {
        ESP_LOGW(TAG, "MQTT client not connected. Dropping message with QoS = 0.");
        return -1;
    }

    if (async)
    {
        // QoS0 async publishes are stored in the esp-mqtt outbox (store=true) so that
        // packet topics keep flowing — enqueue(store=false) produced false-failure
        // semantics that stalled the packet path. The outbox has no size bound of its
        // own (esp-mqtt frees entries only on send-ack or time-based expiry), so on a
        // stalled uplink QoS0 frames pile up on internal heap without limit. Cap it
        // here: if the outbox is already at/over _outbox_limit, drop this QoS0 message
        // and report -2 ("outbox full") so the caller can retry/drop. QoS1/2 (durable,
        // low-rate) are never gated.
        if (qos == 0 && _outbox_limit > 0 && _client != nullptr &&
            esp_mqtt_client_get_outbox_size(_client) >= _outbox_limit)
        {
            _outbox_drops++;
            static unsigned long last_full_log = 0;
            unsigned long now = millis();
            if (now - last_full_log > 5000)
            {
                ESP_LOGW(TAG, "Outbox at cap (%u bytes); dropping QoS0 message to topic %s",
                         (unsigned)_outbox_limit, topic);
                last_full_log = now;
            }
            return -2;
        }

        ESP_LOGV(TAG, "Enqueuing message to topic %s with QoS %d", topic, qos);
        bool store_in_outbox = true;
        int result = esp_mqtt_client_enqueue(_client, topic, payload, length, qos, retain, store_in_outbox);
        if (result < 0) _publish_err++; else _publish_ok++;
        return result;
    }
    else
    {
        ESP_LOGV(TAG, "Publishing message to topic %s with QoS %d", topic, qos);
        // Synchronous write (used for QoS0 packet publishes). A negative result is a real
        // send failure (socket error / network_timeout on a stalled link), tracked here so
        // callers can surface delivery health without per-message logging.
        int result = esp_mqtt_client_publish(_client, topic, payload, length, qos, retain);
        if (result < 0) _publish_err++; else _publish_ok++;
        return result;
    }
}

const char *PsychicMqttClient::getClientId()
{
#if ESP_IDF_VERSION_MAJOR == 5
    return _mqtt_cfg.credentials.client_id;
#else
    return _mqtt_cfg.client_id;
#endif
}

esp_mqtt_client_config_t *PsychicMqttClient::getMqttConfig()
{
    return &_mqtt_cfg;
}

PsychicMqttClient &PsychicMqttClient::setOutboxLimit(size_t bytes)
{
    _outbox_limit = bytes;
    return *this;
}

size_t PsychicMqttClient::getOutboxSize()
{
    if (_client == nullptr)
        return 0;
    int size = esp_mqtt_client_get_outbox_size(_client);
    return size > 0 ? (size_t)size : 0;
}

size_t PsychicMqttClient::getOutboxLimit()
{
    return _outbox_limit;
}

unsigned long PsychicMqttClient::getOutboxDrops()
{
    return _outbox_drops;
}

unsigned long PsychicMqttClient::getPublishOk()
{
    return _publish_ok;
}

unsigned long PsychicMqttClient::getPublishErr()
{
    return _publish_err;
}

void PsychicMqttClient::_onMqttEventStatic(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    // Since this is a static function, we need to cast the first argument (void*) back to the class instance type
    PsychicMqttClient *instance = (PsychicMqttClient *)handler_args;
    instance->_onMqttEvent(base, event_id, event_data);
}

void PsychicMqttClient::_onMqttEvent(esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGV(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch (event_id)
    {
    case MQTT_EVENT_CONNECTED:
        _connected = true;
        _onConnect(event);
        break;
    case MQTT_EVENT_DISCONNECTED:
        _connected = false;
        _onDisconnect(event);
        break;
    case MQTT_EVENT_SUBSCRIBED:
        _onSubscribe(event);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        _onUnsubscribe(event);
        break;
    case MQTT_EVENT_PUBLISHED:
        _onPublish(event);
        break;
    case MQTT_EVENT_DATA:
        _onMessage(event);
        break;
    case MQTT_EVENT_ERROR:
        _connected = false;
        _onError(event);
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

void PsychicMqttClient::_onConnect(esp_mqtt_event_handle_t &event)
{
    ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

    // Resubscribe to all registered topics.
    for (uint8_t i = 0; i < _onMessageUserCallbackCount; ++i)
    {
        OnMessageUserCallback_t &sub = _onMessageUserCallbacks[i];
        if (sub.used && sub.has_topic)
            subscribe(sub.topic, sub.qos);
    }

    for (uint8_t i = 0; i < _onConnectUserCallbackCount; ++i)
    {
        _onConnectUserCallbacks[i](event->session_present);
    }
}

void PsychicMqttClient::_onDisconnect(esp_mqtt_event_handle_t &event)
{
    ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
    for (uint8_t i = 0; i < _onDisconnectUserCallbackCount; ++i)
    {
        _onDisconnectUserCallbacks[i](event->session_present);
    }
    _stopMqttClient = true;
}

void PsychicMqttClient::_onSubscribe(esp_mqtt_event_handle_t &event)
{
    ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
    for (uint8_t i = 0; i < _onSubscribeUserCallbackCount; ++i)
    {
        _onSubscribeUserCallbacks[i](event->msg_id);
    }
}

void PsychicMqttClient::_onUnsubscribe(esp_mqtt_event_handle_t &event)
{
    ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
    for (uint8_t i = 0; i < _onUnsubscribeUserCallbackCount; ++i)
    {
        _onUnsubscribeUserCallbacks[i](event->msg_id);
    }
}

void PsychicMqttClient::_onMessage(esp_mqtt_event_handle_t &event)
{
    // Single-message path: payload fits in the event. No heap use.
    if (event->total_data_len == event->data_len)
    {
        ESP_LOGV(TAG, "MQTT_EVENT_DATA_SINGLE");
        char payload[event->data_len + 1];
        memcpy(payload, event->data, event->data_len);
        payload[event->data_len] = '\0';

        size_t tlen = (size_t)event->topic_len;
        if (tlen >= sizeof(_topic)) tlen = sizeof(_topic) - 1;
        char topic[sizeof(_topic)];
        memcpy(topic, event->topic, tlen);
        topic[tlen] = '\0';

        for (uint8_t i = 0; i < _onMessageUserCallbackCount; ++i)
        {
            OnMessageUserCallback_t &cb = _onMessageUserCallbacks[i];
            if (!cb.used) continue;
            if (!cb.has_topic || _isTopicMatch(topic, cb.topic))
            {
                cb.callback(topic, payload, event->retain, event->qos, event->dup);
            }
        }
        return;
    }

    // Multipart first chunk: remember the topic, begin reassembly in _buffer.
    if (event->current_data_offset == 0)
    {
        ESP_LOGV(TAG, "MQTT_EVENT_DATA_MULTIPART_FIRST");
        if (_buffer == nullptr)
        {
            ESP_LOGE(TAG, "multipart message but no reassembly buffer allocated");
            return;
        }
        if ((size_t)event->total_data_len > _buffer_capacity)
        {
            ESP_LOGE(TAG, "multipart message size %d exceeds reassembly buffer %u", event->total_data_len, (unsigned)_buffer_capacity);
            return;
        }
        memcpy(_buffer, event->data, event->data_len);

        size_t tlen = (size_t)event->topic_len;
        if (tlen >= sizeof(_topic)) tlen = sizeof(_topic) - 1;
        memcpy(_topic, event->topic, tlen);
        _topic[tlen] = '\0';
        return;
    }

    // Multipart final chunk: finalize and dispatch.
    if (event->current_data_offset + event->data_len == event->total_data_len)
    {
        ESP_LOGV(TAG, "MQTT_EVENT_DATA_MULTIPART_LAST");
        if (_buffer == nullptr) return;
        if ((size_t)(event->current_data_offset + event->data_len) > _buffer_capacity) return;
        memcpy(_buffer + event->current_data_offset, event->data, event->data_len);
        _buffer[event->total_data_len] = '\0';

        for (uint8_t i = 0; i < _onMessageUserCallbackCount; ++i)
        {
            OnMessageUserCallback_t &cb = _onMessageUserCallbacks[i];
            if (!cb.used) continue;
            if (!cb.has_topic || _isTopicMatch(_topic, cb.topic))
            {
                cb.callback(_topic, _buffer, event->retain, event->qos, event->dup);
            }
        }
        return;
    }

    // Multipart middle chunk: copy into _buffer at the correct offset.
    if (_buffer == nullptr) return;
    if ((size_t)(event->current_data_offset + event->data_len) > _buffer_capacity) return;
    memcpy(_buffer + event->current_data_offset, event->data, event->data_len);
    ESP_LOGV(TAG, "MQTT_EVENT_DATA_MULTIPART");
}

void PsychicMqttClient::_onPublish(esp_mqtt_event_handle_t &event)
{
    ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
    for (uint8_t i = 0; i < _onPublishUserCallbackCount; ++i)
    {
        _onPublishUserCallbacks[i](event->msg_id);
    }
}

void PsychicMqttClient::_onError(esp_mqtt_event_handle_t &event)
{
    ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
    esp_mqtt_error_codes_t *err = event->error_handle;

    if (err->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
    {
        log_error_if_nonzero("reported from esp-tls", err->esp_tls_last_esp_err);
        log_error_if_nonzero("reported from tls stack", err->esp_tls_stack_err);
        log_error_if_nonzero("captured as transport's socket errno", err->esp_transport_sock_errno);
        ESP_LOGI(TAG, "Last errno string (%s)", strerror(err->esp_transport_sock_errno));
    }
    else if (err->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
    {
        // Broker accepted the socket but rejected the MQTT CONNECT. The return
        // code distinguishes auth failures (4=bad user/pass, 5=not authorized)
        // from server unavailable (3), protocol mismatch (1), and client-id
        // rejection (2) — the key signal for diagnosing a server-side lockout.
        ESP_LOGE(TAG, "Connection refused, return code 0x%x", err->connect_return_code);
    }

    // Forward every error type to the user callbacks. Previously only transport
    // errors were forwarded, so CONNACK rejections (CONNECTION_REFUSED) were
    // silently dropped and never surfaced to the application.
    for (uint8_t i = 0; i < _onErrorUserCallbackCount; ++i)
    {
        _onErrorUserCallbacks[i](*err);
    }
}

// Zero-allocation MQTT topic/subscription match. Supports '+' single-level
// wildcard and trailing '#' multi-level wildcard per MQTT 3.1.1 spec.
bool PsychicMqttClient::_isTopicMatch(const char *topic, const char *subscription)
{
    if (topic == nullptr || subscription == nullptr) return false;

    const char *t = topic;
    const char *s = subscription;

    for (;;)
    {
        // '#' at the start of a level matches everything remaining.
        if (s[0] == '#' && s[1] == '\0')
            return true;

        // Find end of the current level in both strings.
        const char *t_end = strchr(t, '/');
        const char *s_end = strchr(s, '/');
        size_t t_len = t_end ? (size_t)(t_end - t) : strlen(t);
        size_t s_len = s_end ? (size_t)(s_end - s) : strlen(s);

        bool level_matches;
        if (s_len == 1 && s[0] == '+')
        {
            // '+' matches exactly one level (any content, including empty).
            level_matches = true;
        }
        else
        {
            level_matches = (t_len == s_len) && (memcmp(t, s, t_len) == 0);
        }
        if (!level_matches)
            return false;

        // Advance past this level.
        bool t_done = (t_end == nullptr);
        bool s_done = (s_end == nullptr);

        if (t_done && s_done)
            return true;

        if (s_done)
        {
            // Subscription ended but topic has more levels → no match,
            // unless the subscription ended with '+' and topic also has no
            // more levels (already handled above).
            return false;
        }

        if (t_done)
        {
            // Topic ended but subscription has more levels. Only matches if
            // the remainder is exactly '#'.
            return (s_end[1] == '#' && s_end[2] == '\0');
        }

        t = t_end + 1;
        s = s_end + 1;
    }
}
