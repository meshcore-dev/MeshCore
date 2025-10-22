import { BindingsErrorInterface } from '@serialport/bindings-interface';
export declare class BindingsError extends Error implements BindingsErrorInterface {
    canceled: boolean;
    constructor(message: string, { canceled }?: {
        canceled?: boolean | undefined;
    });
}
