import { ErrorCallback, OpenOptions, SerialPortStream } from '@serialport/stream';
import { MockBindingInterface } from '@serialport/binding-mock';
export type SerialPortMockOpenOptions = Omit<OpenOptions<MockBindingInterface>, 'binding'>;
export declare class SerialPortMock extends SerialPortStream<MockBindingInterface> {
    static list: () => Promise<import("@serialport/bindings-interface").PortInfo[]>;
    static readonly binding: MockBindingInterface;
    constructor(options: SerialPortMockOpenOptions, openCallback?: ErrorCallback);
}
