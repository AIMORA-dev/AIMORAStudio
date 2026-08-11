/** Public protocol revision owned jointly with AIMORAService.jl schema generation. */
export const AIMORA_PROTOCOL_REVISION = "0.1.0" as const;

/** Minimal versioned envelope; engineering payload schemas are intentionally deferred. */
export interface AIMORAProtocolEnvelope<TPayload = unknown> {
  readonly protocolRevision: typeof AIMORA_PROTOCOL_REVISION;
  readonly messageType: string;
  readonly payload: TPayload;
}
