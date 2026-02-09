/**
 * Type definitions for node-gst-player
 * A Node.js addon for playing media using GStreamer with WebGL rendering
 */

declare module "node-gst-player" {
  /**
   * GstPlayer class for low-level GStreamer pipeline control
   */
  export class GstPlayer {
    /**
     * Pipeline state constants
     */
    static readonly GST_STATE_VOID_PENDING: number;
    static readonly GST_STATE_NULL: number;
    static readonly GST_STATE_READY: number;
    static readonly GST_STATE_PAUSED: number;
    static readonly GST_STATE_PLAYING: number;

    /**
     * AppSink event constants
     */
    static readonly AppSinkSetup: number;
    static readonly AppSinkNewPreroll: number;
    static readonly AppSinkNewSample: number;
    static readonly AppSinkEos: number;

    /**
     * Parse and create a GStreamer pipeline
     * @param pipelineString - GStreamer pipeline description string
     */
    parseLaunch(pipelineString: string): void;

    /**
     * Add callback to an app sink element
     * @param elementName - Name of the app sink element
     * @param callback - Callback function for events
     */
    addAppSinkCallback(elementName: string, callback: AppSinkCallback): void;

    /**
     * Add probe to an element pad for caps changes
     * @param elementName - Name of the element
     * @param padName - Name of the pad
     * @param callback - Callback function for caps changes
     */
    addCapsProbe(elementName: string, padName: string, callback: CapsProbeCallback): void;

    /**
     * Set the pipeline state
     * @param state - GST_STATE_* constant
     */
    setState(state: number): void;

    /**
     * Send end-of-stream event to the pipeline
     */
    sendEos(): void;
  }

  /**
   * WebGLGstPlayer class for hardware-accelerated video rendering
   */
  export class WebGLGstPlayer {
    /**
     * Create a WebGL video player
     * @param canvas - HTML canvas element for rendering
     */
    constructor(canvas: HTMLCanvasElement);

    /**
     * Start playback with a GStreamer pipeline
     * @param pipelineDesc - GStreamer pipeline description
     */
    start(pipelineDesc: string): void;

    /**
     * Stop playback by sending EOS event
     */
    stop(): void;

    /**
     * Get total frames rendered
     * @returns Frame count
     */
    getFrameCount(): number;

    /**
     * Reference to the underlying GstPlayer instance
     */
    player: GstPlayer;
  }

  /**
   * Video format information
   */
  export interface VideoInfo {
    width: number;
    height: number;
    pixelFormat: string;
    mediaType: string;
    caps?: string;
    fpsNum?: number;
    fpsDen?: number;
    parNum?: number;
    parDen?: number;
  }

  /**
   * Audio format information
   */
  export interface AudioInfo {
    channels: number;
    samplingRate: number;
    sampleSize: number;
    format: string;
    mediaType: string;
    caps?: string;
  }

  /**
   * Media info (video or audio)
   */
  export type MediaInfo = VideoInfo | AudioInfo;

  /**
   * AppSink callback event types
   */
  export type AppSinkEvent =
    | typeof GstPlayer.AppSinkSetup
    | typeof GstPlayer.AppSinkNewPreroll
    | typeof GstPlayer.AppSinkNewSample
    | typeof GstPlayer.AppSinkEos;

  /**
   * AppSink callback function type
   */
  export type AppSinkCallback = (event: AppSinkEvent, ...args: any[]) => void;

  /**
   * Caps probe callback function type
   */
  export type CapsProbeCallback = (caps: MediaInfo) => void;
}
