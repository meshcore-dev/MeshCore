/// <reference types="node" />
/// <reference types="node" />
import { Transform, TransformCallback } from 'stream';
/**
 * Parse the CCTalk protocol
 * @extends Transform
 *
 * A transform stream that emits CCTalk packets as they are received.
 */
export declare class CCTalkParser extends Transform {
    array: number[];
    cursor: number;
    lastByteFetchTime: number;
    maxDelayBetweenBytesMs: number;
    constructor(maxDelayBetweenBytesMs?: number);
    _transform(buffer: Buffer, encoding: BufferEncoding, cb: TransformCallback): void;
}
