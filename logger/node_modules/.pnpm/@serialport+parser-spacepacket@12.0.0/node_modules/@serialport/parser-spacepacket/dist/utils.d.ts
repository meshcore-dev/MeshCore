export declare const HEADER_LENGTH = 6;
export interface SpacePacketHeader {
    versionNumber: string | number;
    identification: {
        apid: number;
        secondaryHeader: number;
        type: number;
    };
    sequenceControl: {
        packetName: number;
        sequenceFlags: number;
    };
    dataLength: number;
}
export interface SpacePacket {
    header: SpacePacketHeader;
    secondaryHeader?: {
        timeCode?: string;
        ancillaryData?: string;
    };
    data: string;
}
/**
 * Converts a Buffer of any length to an Object representation of a Space Packet header, provided
 * the received data is in the correct format.
 * @param buf - The buffer containing the Space Packet Header Data
 */
export declare const convertHeaderBufferToObj: (buf: Buffer) => SpacePacketHeader;
