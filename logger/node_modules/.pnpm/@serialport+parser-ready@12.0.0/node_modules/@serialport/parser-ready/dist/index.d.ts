/// <reference types="node" />
/// <reference types="node" />
import { Transform, TransformCallback, TransformOptions } from 'stream';
export interface ReadyParserOptions extends TransformOptions {
    /** delimiter to use to detect the input is ready */
    delimiter: string | Buffer | number[];
}
/**
 * A transform stream that waits for a sequence of "ready" bytes before emitting a ready event and emitting data events
 *
 * To use the `Ready` parser provide a byte start sequence. After the bytes have been received a ready event is fired and data events are passed through.
 */
export declare class ReadyParser extends Transform {
    delimiter: Buffer;
    readOffset: number;
    ready: boolean;
    constructor({ delimiter, ...options }: ReadyParserOptions);
    _transform(chunk: Buffer, encoding: BufferEncoding, cb: TransformCallback): void;
}
