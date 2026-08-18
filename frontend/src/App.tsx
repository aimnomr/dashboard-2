import { PlaceholderPanel } from './components/Panel'
import { StatusBar } from './components/StatusBar'
import { useNow } from './hooks/useNow'
import { useTelemetry } from './hooks/useTelemetry'
import { AltitudePanel } from './panels/AltitudePanel'

export default function App() {
  const telemetry = useTelemetry()
  // Ticks independently of incoming data so "time since last packet" keeps counting
  // when the link dies — which is exactly when it matters.
  const now = useNow(250)

  return (
    <div className="app">
      <StatusBar telemetry={telemetry} now={now} />

      <main className="grid">
        <AltitudePanel history={telemetry.history} latest={telemetry.latest} />
        <PlaceholderPanel title="Ground track" area="track" />
        <PlaceholderPanel title="Attitude" area="attitude" />
        <PlaceholderPanel title="GPS" area="gps" />
        <PlaceholderPanel title="Speed" area="speed" />
        <PlaceholderPanel title="Environment" area="env" />
        <PlaceholderPanel title="Eject" area="eject" />
        <PlaceholderPanel title="Raw feed" area="raw" />
      </main>
    </div>
  )
}
