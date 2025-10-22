/// <reference types="node" />
/// <reference types="node" />
import { write } from 'fs';
import { LinuxPortBinding } from './linux';
import { DarwinPortBinding } from './darwin';
declare const writeAsync: typeof write.__promisify__;
interface UnixWriteOptions {
    binding: LinuxPortBinding | DarwinPortBinding;
    buffer: Buffer;
    offset?: number;
    fsWriteAsync?: typeof writeAsync;
}
export declare const unixWrite: ({ binding, buffer, offset, fsWriteAsync }: UnixWriteOptions) => Promise<void>;
export {};
