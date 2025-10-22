/// <reference types="node" />
import { BindingPortInterface } from '.';
import { BindingInterface, OpenOptions, PortStatus, SetOptions, UpdateOptions } from '@serialport/bindings-interface';
import { Poller } from './poller';
export interface DarwinOpenOptions extends OpenOptions {
    /** Defaults to none */
    parity?: 'none' | 'even' | 'odd';
    /** see [`man termios`](http://linux.die.net/man/3/termios) defaults to 1 */
    vmin?: number;
    /** see [`man termios`](http://linux.die.net/man/3/termios) defaults to 0 */
    vtime?: number;
}
export type DarwinBindingInterface = BindingInterface<DarwinPortBinding, DarwinOpenOptions>;
export declare const DarwinBinding: DarwinBindingInterface;
/**
 * The Darwin binding layer for OSX
 */
export declare class DarwinPortBinding implements BindingPortInterface {
    readonly openOptions: Required<DarwinOpenOptions>;
    readonly poller: Poller;
    private writeOperation;
    fd: null | number;
    constructor(fd: number, options: Required<DarwinOpenOptions>);
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
