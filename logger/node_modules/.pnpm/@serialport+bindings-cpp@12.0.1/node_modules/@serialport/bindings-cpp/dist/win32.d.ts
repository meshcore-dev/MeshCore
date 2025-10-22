/// <reference types="node" />
import { BindingPortInterface } from '.';
import { BindingInterface, OpenOptions, PortStatus, SetOptions, UpdateOptions } from '@serialport/bindings-interface';
export interface WindowsOpenOptions extends OpenOptions {
    /** Device parity defaults to none */
    parity?: 'none' | 'even' | 'odd' | 'mark' | 'space';
    /** RTS mode defaults to handshake */
    rtsMode?: 'handshake' | 'enable' | 'toggle';
}
export type WindowsBindingInterface = BindingInterface<WindowsPortBinding, WindowsOpenOptions>;
export declare const WindowsBinding: WindowsBindingInterface;
/**
 * The Windows binding layer
 */
export declare class WindowsPortBinding implements BindingPortInterface {
    fd: null | number;
    writeOperation: Promise<void> | null;
    openOptions: Required<OpenOptions>;
    constructor(fd: number, options: Required<OpenOptions>);
    get isOpen(): boolean;
    close(): Promise<void>;
    read(buffer: Buffer, offset: number, length: number): Promise<{
        buffer: Buffer;
        bytesRead: number;
    }>;
    write(buffer: Buffer): Promise<void>;
    update(options: UpdateOptions): Promise<void>;
    set(options: SetOptions): Promise<void>;
    get(): Promise<PortStatus>;
    getBaudRate(): Promise<{
        baudRate: number;
    }>;
    flush(): Promise<void>;
    drain(): Promise<void>;
}
