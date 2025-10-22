/// <reference types="node" />
/// <reference types="node" />
import { Transform, TransformCallback, TransformOptions } from 'stream';
export interface SlipDecoderOptions extends TransformOptions {
    /** Custom start byte */
    START?: number;
    /** Custom start escape byte */
    ESC_START?: number;
    /** custom escape byte */
    ESC?: number;
    /** custom end byte */
    END?: number;
    /** custom escape end byte */
    ESC_END?: number;
    /** custom escape escape byte */
    ESC_ESC?: number;
}
/**
 * A transform stream that decodes slip encoded data.
 * @extends Transform
 *
 * Runs in O(n) time, stripping out slip encoding and emitting decoded data. Optionally custom slip escape and delimiters can be provided.
 */
export declare class SlipDecoder extends Transform {
    opts: {
        START: number | undefined;
        ESC: number;
        END: number;
        ESC_START: number | undefined;
        ESC_END: number;
        ESC_ESC: number;
    };
    buffer: Buffer;
    escape: boolean;
    start: boolean;
    constructor(options?: SlipDecoderOptions);
    _transform(chunk: Buffer, encoding: BufferEncoding, cb: TransformCallback): void;
    _flush(cb: TransformCallback): void;
}
