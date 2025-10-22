/// <reference types="node" />
/// <reference types="node" />
import { read as fsRead } from 'fs';
import { LinuxPortBinding } from './linux';
import { DarwinPortBinding } from './darwin';
declare const readAsync: typeof fsRead.__promisify__;
interface UnixReadOptions {
    binding: LinuxPortBinding | DarwinPortBinding;
    buffer: Buffer;
    offset: number;
    length: number;
    fsReadAsync?: typeof readAsync;
}
export declare const unixRead: ({ binding, buffer, offset, length, fsReadAsync, }: UnixReadOptions) => Promise<{
    buffer: Buffer;
    bytesRead: number;
}>;
export {};
