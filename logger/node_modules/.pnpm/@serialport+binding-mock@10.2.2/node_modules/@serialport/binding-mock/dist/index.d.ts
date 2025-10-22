/// <reference types="node" />

import { BindingInterface } from '@serialport/bindings-interface';
import { BindingPortInterface } from '@serialport/bindings-interface';
import { OpenOptions } from '@serialport/bindings-interface';
import { PortInfo } from '@serialport/bindings-interface';
import { PortStatus } from '@serialport/bindings-interface';
import { SetOptions } from '@serialport/bindings-interface';
import { UpdateOptions } from '@serialport/bindings-interface';

export declare class CanceledError extends Error {
    canceled: true;
    constructor(message: string);
}

export declare interface CreatePortOptions {
    echo?: boolean;
    record?: boolean;
    readyData?: Buffer;
    maxReadSize?: number;
    manufacturer?: string;
    vendorId?: string;
    productId?: string;
}

export declare const MockBinding: MockBindingInterface;

export declare interface MockBindingInterface extends BindingInterface<MockPortBinding> {
    reset(): void;
    createPort(path: string, opt?: CreatePortOptions): void;
}

/**
 * Mock bindings for pretend serialport access
 */
export declare class MockPortBinding implements BindingPortInterface {
    readonly openOptions: Required<OpenOptions>;
    readonly port: MockPortInternal;
    private pendingRead;
    lastWrite: null | Buffer;
    recording: Buffer;
    writeOperation: null | Promise<void>;
    isOpen: boolean;
    serialNumber?: string;
    constructor(port: MockPortInternal, openOptions: Required<OpenOptions>);
    emitData(data: Buffer | string): void;
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

export declare interface MockPortInternal {
    data: Buffer;
    echo: boolean;
    record: boolean;
    info: PortInfo;
    maxReadSize: number;
    readyData?: Buffer;
    openOpt?: OpenOptions;
}

export { }
