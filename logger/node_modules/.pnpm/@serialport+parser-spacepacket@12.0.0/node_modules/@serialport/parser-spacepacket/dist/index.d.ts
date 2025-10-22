/// <reference types="node" />
/// <reference types="node" />
import { Transform, TransformCallback, TransformOptions } from 'stream';
import { SpacePacket, SpacePacketHeader } from './utils';
export { SpacePacket, SpacePacketHeader };
/** The optional configuration object, only needed if either of the two fields of the secondary header need their length defined */
export interface SpacePacketOptions extends Omit<TransformOptions, 'objectMode'> {
    /** The length of the Time Code Field in octets, if present */
    timeCodeFieldLength?: number;
    /** The length of the Ancillary Data Field in octets, if present */
    ancillaryDataFieldLength?: number;
}
/**
 * A Transform stream that accepts a stream of octet data and converts it into an object
 * representation of a CCSDS Space Packet. See https://public.ccsds.org/Pubs/133x0b2e1.pdf for a
 * description of the Space Packet format.
 */
export declare class SpacePacketParser extends Transform {
    timeCodeFieldLength: number;
    ancillaryDataFieldLength: number;
    dataBuffer: Buffer;
    headerBuffer: Buffer;
    dataLength: number;
    expectingHeader: boolean;
    dataSlice: number;
    header?: SpacePacketHeader;
    /**
     * A Transform stream that accepts a stream of octet data and emits object representations of
     * CCSDS Space Packets once a packet has been completely received.
     * @param {Object} [options] Configuration options for the stream
     * @param {Number} options.timeCodeFieldLength The length of the time code field within the data
     * @param {Number} options.ancillaryDataFieldLength The length of the ancillary data field within the data
     */
    constructor(options?: SpacePacketOptions);
    /**
     * Bundle the header, secondary header if present, and the data into a JavaScript object to emit.
     * If more data has been received past the current packet, begin the process of parsing the next
     * packet(s).
     */
    pushCompletedPacket(): void;
    /**
     * Build the Stream's headerBuffer property from the received Buffer chunk; extract data from it
     * if it's complete. If there's more to the chunk than just the header, initiate handling the
     * packet data.
     * @param chunk -  Build the Stream's headerBuffer property from
     */
    extractHeader(chunk: Buffer): void;
    _transform(chunk: Buffer, encoding: BufferEncoding, cb: TransformCallback): void;
    _flush(cb: TransformCallback): void;
}
