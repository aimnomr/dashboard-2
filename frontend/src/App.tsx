import { useState } from 'react'
import { StatusBar } from './components/StatusBar'
import { useNow } from './hooks/useNow'
import { useTelemetry } from './hooks/useTelemetry'
import { ChannelsView } from './views/ChannelsView'
import type { View } from './types/telemetry'
import { AltitudePanel } from './panels/AltitudePanel'
import { AttitudePanel } from './panels/AttitudePanel'
import { EjectPanel } from './panels/EjectPanel'
import { EnvironmentPanel } from './panels/EnvironmentPanel'
import { GpsPanel } from './panels/GpsPanel'
import { GroundTrackPanel } from './panels/GroundTrackPanel'
import { RawFeedPanel } from './panels/RawFeedPanel'
import { SpeedPanel } from './panels/SpeedPanel'

export default function App() {
  const telemetry = useTelemetry()
  // Ticks independently of incoming data so "time since last packet" keeps counting
  // when the link dies — which is exactly when it matters.
  const now = useNow(250)
  // Flight is the default and stays the default. The channels view is for reading a
  // flight afterwards or checking a replay; nobody should have to switch back to it
  // while something is in the air.
  const [view, setView] = useState<View>('flight')

  return (
    <div className="app">
      <StatusBar telemetry={telemetry} now={now} view={view} onViewChange={setView} />

      {view === 'channels' ? (
        <main className="view-scroll">
          <ChannelsView history={telemetry.history} />
        </main>
      ) : (
        <main className="grid">
          <AltitudePanel history={telemetry.history} latest={telemetry.latest} />
          <GroundTrackPanel history={telemetry.history} latest={telemetry.latest} />
          <AttitudePanel latest={telemetry.latest} history={telemetry.history} />
          <GpsPanel latest={telemetry.latest} />
          <SpeedPanel history={telemetry.history} latest={telemetry.latest} />
          <EnvironmentPanel history={telemetry.history} latest={telemetry.latest} />
          <EjectPanel
            latest={telemetry.latest}
            lastAck={telemetry.lastAck}
            now={now}
            sendCommand={telemetry.sendCommand}
          />
          <RawFeedPanel raw={telemetry.raw} />
        </main>
      )}
    </div>
  )
}
