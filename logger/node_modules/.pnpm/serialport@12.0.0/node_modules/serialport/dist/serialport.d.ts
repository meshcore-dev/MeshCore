import { ErrorCallback, SerialPortStream, StreamOptions } from '@serialport/stream';
import { AutoDetectTypes, OpenOptionsFromBinding } from '@serialport/bindings-cpp';
export type SerialPortOpenOptions<T extends AutoDetectTypes> = Omit<StreamOptions<T>, 'binding'> & OpenOptionsFromBinding<T>;
export declare class SerialPort<T extends AutoDetectTypes = AutoDetectTypes> extends SerialPortStream<T> {
    static list: () => Promise<import("@serialport/bindings-interface").PortInfo[]>;
    static readonly binding: AutoDetectTypes;
    constructor(options: SerialPortOpenOptions<T>, openCallback?: ErrorCallback);
}
