/// <reference types="node" />
/// <reference types="node" />
import { Duplex } from 'stream';
import { SetOptions, BindingInterface, PortInterfaceFromBinding, OpenOptionsFromBinding } from '@serialport/bindings-interface';
export declare class DisconnectedError extends Error {
    disconnected: true;
    constructor(message: string);
}
interface InternalSettings<T extends BindingInterface> {
    binding: T;
    autoOpen: boolean;
    endOnClose: boolean;
    highWaterMark: number;
}
/**
 * A callback called with an error or an object with the modem line values (cts, dsr, dcd).
 */
export type ErrorCallback = (err: Error | null) => void;
export type ModemBitsCallback = (err: Error | null, options?: {
    cts: boolean;
    dsr: boolean;
    dcd: boolean;
}) => void;
export type OpenOptions<T extends BindingInterface = BindingInterface> = StreamOptions<T> & OpenOptionsFromBinding<T>;
/**
 * Options to open a port
 */
export interface StreamOptions<T extends BindingInterface> {
    /**
     * The hardware access binding. `Bindings` are how Node-Serialport talks to the underlying system. If you're using the `serialport` package, this defaults to `'@serialport/bindings-cpp'` which auto detects Windows (`WindowsBinding`), Linux (`LinuxBinding`) and OS X (`DarwinBinding`) and load the appropriate module for your system.
     */
    binding: T;
    /** Automatically opens the port defaults to true*/
    autoOpen?: boolean;
    /**
     * The size of the read and write buffers defaults to 64k
     */
    highWaterMark?: number;
    /**
     * Emit 'end' on port close defaults false
     */
    endOnClose?: boolean;
}
export declare class SerialPortStream<T extends BindingInterface = BindingInterface> extends Duplex {
    port?: PortInterfaceFromBinding<T>;
    private _pool;
    private _kMinPoolSpace;
    opening: boolean;
    closing: boolean;
    readonly settings: InternalSettings<T> & OpenOptionsFromBinding<T>;
    /**
     * Create a new serial port object for the `path`. In the case of invalid arguments or invalid options, when constructing a new SerialPort it will throw an error. The port will open automatically by default, which is the equivalent of calling `port.open(openCallback)` in the next tick. You can disable this by setting the option `autoOpen` to `false`.
     * @emits open
     * @emits data
     * @emits close
     * @emits error
     */
    constructor(options: OpenOptions<T>, openCallback?: ErrorCallback);
    get path(): string;
    get baudRate(): number;
    get isOpen(): boolean;
    private _error;
    private _asyncError;
    /**
     * Opens a connection to the given serial port.
     * @param {ErrorCallback=} openCallback - Called after a connection is opened. If this is not provided and an error occurs, it will be emitted on the port's `error` event.
     * @emits open
     */
    open(openCallback?: ErrorCallback): void;
    /**
     * Changes the baud rate for an open port. Emits an error or calls the callback if the baud rate isn't supported.
     * @param {object=} options Only supports `baudRate`.
     * @param {number=} [options.baudRate] The baud rate of the port to be opened. This should match one of the commonly available baud rates, such as 110, 300, 1200, 2400, 4800, 9600, 14400, 19200, 38400, 57600, or 115200. Custom rates are supported best effort per platform. The device connected to the serial port is not guaranteed to support the requested baud rate, even if the port itself supports that baud rate.
     * @param {ErrorCallback=} [callback] Called once the port's baud rate changes. If `.update` is called without a callback, and there is an error, an error event is emitted.
     * @returns {undefined}
     */
    update(options: {
        baudRate: number;
    }, callback?: ErrorCallback): void;
    /**
     * Writes data to the given serial port. Buffers written data if the port is not open.
  
    The write operation is non-blocking. When it returns, data might still not have been written to the serial port. See `drain()`.
  
    Some devices, like the Arduino, reset when you open a connection to them. In such cases, immediately writing to the device will cause lost data as they wont be ready to receive the data. This is often worked around by having the Arduino send a "ready" byte that your Node program waits for before writing. You can also often get away with waiting around 400ms.
  
    If a port is disconnected during a write, the write will error in addition to the `close` event.
  
    From the [stream docs](https://nodejs.org/api/stream.html#stream_writable_write_chunk_encoding_callback) write errors don't always provide the error in the callback, sometimes they use the error event.
    > If an error occurs, the callback may or may not be called with the error as its first argument. To reliably detect write errors, add a listener for the 'error' event.
  
    In addition to the usual `stream.write` arguments (`String` and `Buffer`), `write()` can accept arrays of bytes (positive numbers under 256) which is passed to `Buffer.from([])` for conversion. This extra functionality is pretty sweet.
  
    * @param  {(string|array|buffer)} data Accepts a [`Buffer`](http://nodejs.org/api/buffer.html) object, or a type that is accepted by the `Buffer.from` method (e.g. an array of bytes or a string).
     * @param  {string=} encoding The encoding, if chunk is a string. Defaults to `'utf8'`. Also accepts `'ascii'`, `'base64'`, `'binary'`, and `'hex'` See [Buffers and Character Encodings](https://nodejs.org/api/buffer.html#buffer_buffers_and_character_encodings) for all available options.
     * @param  {function=} errorCallback Called once the write operation finishes. Data may not yet be flushed to the underlying port. No optional Error.
     * @returns {boolean} `false` if the stream wishes for the calling code to wait for the `'drain'` event to be emitted before continuing to write additional data; otherwise `true`.
     */
    write(chunk: any, encoding?: BufferEncoding, cb?: (error: Error | null | undefined) => void): boolean;
    write(chunk: any, cb?: (error: Error | null | undefined) => void): boolean;
    _write(data: Buffer, encoding: BufferEncoding | undefined, callback: (error: Error | null) => void): void;
    _writev(data: Array<{
        chunk: Buffer;
        encoding: BufferEncoding;
    }>, callback: ErrorCallback): void;
    _read(bytesToRead: number): void;
    _disconnected(err: Error): void;
    /**
     * Closes an open connection.
     *
     * If there are in progress writes when the port is closed the writes will error.
     * @param {ErrorCallback} callback Called once a connection is closed.
     * @param {Error} disconnectError used internally to propagate a disconnect error
     */
    close(callback?: ErrorCallback | undefined, disconnectError?: Error | null): void;
    /**
     * Set control flags on an open port. Uses [`SetCommMask`](https://msdn.microsoft.com/en-us/library/windows/desktop/aa363257(v=vs.85).aspx) for Windows and [`ioctl`](http://linux.die.net/man/4/tty_ioctl) for OS X and Linux.
     *
     * All options are operating system default when the port is opened. Every flag is set on each call to the provided or default values. If options isn't provided default options is used.
     */
    set(options: SetOptions, callback?: ErrorCallback): void;
    /**
     * Returns the control flags (CTS, DSR, DCD) on the open port.
     * Uses [`GetCommModemStatus`](https://msdn.microsoft.com/en-us/library/windows/desktop/aa363258(v=vs.85).aspx) for Windows and [`ioctl`](http://linux.die.net/man/4/tty_ioctl) for mac and linux.
     */
    get(callback: ModemBitsCallback): void;
    /**
     * Flush discards data received but not read, and written but not transmitted by the operating system. For more technical details, see [`tcflush(fd, TCIOFLUSH)`](http://linux.die.net/man/3/tcflush) for Mac/Linux and [`FlushFileBuffers`](http://msdn.microsoft.com/en-us/library/windows/desktop/aa364439) for Windows.
     */
    flush(callback?: ErrorCallback): void;
    /**
     * Waits until all output data is transmitted to the serial port. After any pending write has completed it calls [`tcdrain()`](http://linux.die.net/man/3/tcdrain) or [FlushFileBuffers()](https://msdn.microsoft.com/en-us/library/windows/desktop/aa364439(v=vs.85).aspx) to ensure it has been written to the device.
    * @example
    Write the `data` and wait until it has finished transmitting to the target serial port before calling the callback. This will queue until the port is open and writes are finished.
  
    ```js
    function writeAndDrain (data, callback) {
      port.write(data);
      port.drain(callback);
    }
    ```
    */
    drain(callback?: ErrorCallback): void;
}
export {};
/**
 * The `error` event's callback is called with an error object whenever there is an error.
 * @event error
 */
/**
 * The `open` event's callback is called with no arguments when the port is opened and ready for writing. This happens if you have the constructor open immediately (which opens in the next tick) or if you open the port manually with `open()`. See [Useage/Opening a Port](#opening-a-port) for more information.
 * @event open
 */
/**
 * Request a number of bytes from the SerialPort. The `read()` method pulls some data out of the internal buffer and returns it. If no data is available to be read, null is returned. By default, the data is returned as a `Buffer` object unless an encoding has been specified using the `.setEncoding()` method.
 * @method SerialPort.prototype.read
 * @param {number=} size Specify how many bytes of data to return, if available
 * @returns {(string|Buffer|null)} The data from internal buffers
 */
/**
 * Listening for the `data` event puts the port in flowing mode. Data is emitted as soon as it's received. Data is a `Buffer` object with a varying amount of data in it. The `readLine` parser converts the data into string lines. See the [parsers](https://serialport.io/docs/api-parsers-overview) section for more information on parsers, and the [Node.js stream documentation](https://nodejs.org/api/stream.html#stream_event_data) for more information on the data event.
 * @event data
 */
/**
 * The `close` event's callback is called with no arguments when the port is closed. In the case of a disconnect it will be called with a Disconnect Error object (`err.disconnected == true`). In the event of a close error (unlikely), an error event is triggered.
 * @event close
 */
/**
 * The `pause()` method causes a stream in flowing mode to stop emitting 'data' events, switching out of flowing mode. Any data that becomes available remains in the internal buffer.
 * @method SerialPort.prototype.pause
 * @see resume
 * @returns `this`
 */
/**
 * The `resume()` method causes an explicitly paused, `Readable` stream to resume emitting 'data' events, switching the stream into flowing mode.
 * @method SerialPort.prototype.resume
 * @see pause
 * @returns `this`
 */
