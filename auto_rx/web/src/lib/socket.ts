import { useEffect, useRef, useState } from "react";
import { io, type Socket } from "socket.io-client";

let _socket: Socket | null = null;

export function getSocket(): Socket {
  if (!_socket) {
    _socket = io(`${window.location.origin}/update_status`, {
      path: "/socket.io",
      transports: ["polling", "websocket"],
    });
  }
  return _socket;
}

export function useSocketConnected() {
  const [connected, setConnected] = useState(false);
  useEffect(() => {
    const s = getSocket();
    const onC = () => { setConnected(true); s.emit("client_connected"); };
    const onD = () => setConnected(false);
    s.on("connect", onC); s.on("disconnect", onD);
    if (s.connected) setConnected(true);
    return () => { s.off("connect", onC); s.off("disconnect", onD); };
  }, []);
  return connected;
}

/**
 * Subscribe to a socket.io event. The handler is held in a ref so we can
 * register the listener once and still see the latest closure on every call
 * — without this, callers passing `deps=[]` would capture stale state.
 */
export function useSocketEvent<T = any>(event: string, handler: (data: T) => void) {
  const ref = useRef(handler);
  useEffect(() => { ref.current = handler; });
  useEffect(() => {
    const s = getSocket();
    const fn = (data: T) => ref.current(data);
    s.on(event, fn);
    return () => { s.off(event, fn); };
  }, [event]);
}
