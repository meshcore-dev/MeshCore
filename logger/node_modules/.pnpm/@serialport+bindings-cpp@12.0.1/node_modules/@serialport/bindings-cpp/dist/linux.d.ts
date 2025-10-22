/// <reference types="node" />
import { Poller } from './poller';
import { BindingInterface, OpenOptions, PortStatus, SetOptions, UpdateOptions } from '@serialport/bindings-interface';
import { BindingPortInterface } from '.';
export interface LinuxOpenOptions extends OpenOptions {
    /** Defaults to none */
    parity?: 'none' | 'even' | 'odd';
    /** see [`man termios`](http://linux.die.net/man/3/termios) defaults to 1 */
    vmin?: number;
    /** see [`man termios`](http://linux.die.net/man/3/termios) defaults to 0 */
    vtime?: number;
}
export interface LinuxPortStatus extends PortStatus {
    lowLatency: boolean;
}
export interface LinuxSetOptions extends SetOptions {
    /** Low latency mode */
    lowLatency?: boolean;
}
export type LinuxBindingInterface = BindingInterface<LinuxPortBinding, LinuxOpenOptions>;
export declare const LinuxBinding: LinuxBindingInterface;
/**
 * The linux binding layer
 */
export declare class LinuxPortBinding implements BindingPortInterface {
    readonly openOptions: Required<LinuxOpenOptions>;
    readonly poller: Poller;
    private writeOperation;
    fd: number | null;
    constructor(fd: number, openOptions: Required<LinuxOpenOptions>);
    get isOpen(): boolean;
    close(): Promise<void>;
    read(buffer: Buffer, offset: number, length: number): Promise<{
        buffer: Buffer;
        bytesRead: number;
    }>;
    write(buffer: Buffer): Promise<void>;
    update(options: UpdateOptions): Promise<void>;
    set(options: LinuxSetOptions): Promise<void>;
    get(): Promise<LinuxPortStatus>;
    getBaudRate(): Promise<{
        baudRate: number;
    }>;
    flush(): Promise<void>;
    drain(): Promise<void>;
}
