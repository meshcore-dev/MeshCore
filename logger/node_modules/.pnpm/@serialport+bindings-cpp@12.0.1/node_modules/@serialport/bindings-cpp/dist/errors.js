"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.BindingsError = void 0;
class BindingsError extends Error {
    constructor(message, { canceled = false } = {}) {
        super(message);
        this.canceled = canceled;
    }
}
exports.BindingsError = BindingsError;
