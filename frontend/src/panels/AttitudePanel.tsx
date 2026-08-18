import { useEffect, useRef, useState } from 'react'
import * as THREE from 'three'
import { Panel } from '../components/Panel'
import { computeAttitude, integrateSpin } from '../lib/attitude'
import { createReference, createRocket, LIVE_COLORS, MUTED_COLORS } from '../lib/rocketModel'
import type { FrameRecord } from '../types/telemetry'

const WORLD_UP = new THREE.Vector3(0, 1, 0)

/**
 * Body-frame accelerometer reading, expressed in the model's local axes.
 *
 * The MPU6050 reads +1 g on the axis pointing upward, so at rest the acceleration
 * vector IS the local "up" direction. The model's long axis is +Y, while the vehicle's
 * body up is +Z, so body (x, y, z) maps to model (x, z, y).
 */
function bodyUpInModelSpace(ax: number, ay: number, az: number): THREE.Vector3 {
  return new THREE.Vector3(ax, az, ay).normalize()
}

export function AttitudePanel({ latest }: { latest: FrameRecord | null }) {
  const hostRef = useRef<HTMLDivElement | null>(null)
  const sceneRef = useRef<{
    renderer: THREE.WebGLRenderer
    scene: THREE.Scene
    camera: THREE.PerspectiveCamera
    rocket: ReturnType<typeof createRocket>
    render: () => void
  } | null>(null)

  const spinRef = useRef(0)
  const lastFrameAtRef = useRef<number | null>(null)
  const [webglFailed, setWebglFailed] = useState(false)

  const attitude = latest ? computeAttitude(latest.frame) : null

  // ---- scene setup, once ---------------------------------------------------
  useEffect(() => {
    const host = hostRef.current
    if (!host) return

    let renderer: THREE.WebGLRenderer
    try {
      renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true })
    } catch {
      setWebglFailed(true)
      return
    }

    // Capped: a field laptop on battery gains nothing from rendering a small dial at
    // 3x device pixel ratio.
    renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2))
    renderer.setClearAlpha(0)
    host.appendChild(renderer.domElement)
    renderer.domElement.style.position = 'absolute'
    renderer.domElement.style.inset = '0'
    renderer.domElement.style.width = '100%'
    renderer.domElement.style.height = '100%'

    const scene = new THREE.Scene()
    const camera = new THREE.PerspectiveCamera(34, 1, 0.1, 100)
    camera.position.set(2.3, 1.35, 2.9)
    camera.lookAt(0, 0, 0)

    scene.add(new THREE.AmbientLight(0xffffff, 1.5))
    const key = new THREE.DirectionalLight(0xffffff, 2.2)
    key.position.set(3, 5, 4)
    scene.add(key)
    const rim = new THREE.DirectionalLight(0xffffff, 0.7)
    rim.position.set(-4, 1, -3)
    scene.add(rim)

    const rocket = createRocket()
    scene.add(rocket.group)
    const reference = createReference()
    scene.add(reference.group)

    const resize = () => {
      const width = host.clientWidth
      const height = host.clientHeight
      if (width === 0 || height === 0) return
      renderer.setSize(width, height, false)
      camera.aspect = width / height
      camera.updateProjectionMatrix()
      renderer.render(scene, camera)
    }

    const render = () => renderer.render(scene, camera)

    sceneRef.current = { renderer, scene, camera, rocket, render }
    resize()

    const observer = new ResizeObserver(resize)
    observer.observe(host)

    return () => {
      observer.disconnect()
      rocket.dispose()
      reference.dispose()
      renderer.dispose()
      if (renderer.domElement.parentNode === host) host.removeChild(renderer.domElement)
      sceneRef.current = null
    }
  }, [])

  // ---- pose update, once per frame -----------------------------------------
  // Rendering is driven by incoming telemetry (1 Hz) rather than a continuous
  // requestAnimationFrame loop. A dial that redraws 60 times a second to show data that
  // changes once a second would heat and drain a laptop sitting in a field.
  useEffect(() => {
    const ctx = sceneRef.current
    if (!ctx || !latest || !attitude) return

    const previousAt = lastFrameAtRef.current
    lastFrameAtRef.current = latest.receivedAt
    if (previousAt !== null) {
      spinRef.current = integrateSpin(
        spinRef.current,
        latest.frame.gz,
        (latest.receivedAt - previousAt) / 1000,
      )
    }

    const { ax, ay, az } = latest.frame
    const up = bodyUpInModelSpace(ax, ay, az)

    // Minimal rotation carrying the measured up-direction onto world up. This fixes
    // tilt exactly — which is all the accelerometer can determine — and leaves rotation
    // about vertical arbitrary, which is honest: there is no heading reference.
    const tilt = up.lengthSq() > 0
      ? new THREE.Quaternion().setFromUnitVectors(up, WORLD_UP)
      : new THREE.Quaternion()

    const spin = new THREE.Quaternion().setFromAxisAngle(WORLD_UP, spinRef.current)
    ctx.rocket.group.quaternion.copy(tilt.multiply(spin))
    ctx.rocket.setColors(attitude.reliable ? LIVE_COLORS : MUTED_COLORS)
    ctx.render()
  }, [latest, attitude])

  return (
    <Panel
      title="Attitude"
      area="attitude"
      note={attitude ? `${attitude.magnitude.toFixed(2)} g` : undefined}
    >
      <div className="attitude">
        <div className="canvas-host attitude__dial" ref={hostRef}>
          {webglFailed && <div className="attitude__nogl">WebGL unavailable</div>}
        </div>
        <div className="attitude__readout">
          <div>
            <span className="label">Pitch</span>
            <div className="value numeric">
              {attitude ? `${attitude.pitch.toFixed(0)}°` : '—'}
            </div>
          </div>
          <div>
            <span className="label">Roll</span>
            <div className="value numeric">
              {attitude ? `${attitude.roll.toFixed(0)}°` : '—'}
            </div>
          </div>
          <div>
            <span className="label">Spin</span>
            <div className="value numeric">
              {attitude ? `${attitude.spinRate.toFixed(0)}` : '—'}
              <span style={{ fontSize: '0.55em', color: 'var(--text-dim)' }}> °/s</span>
            </div>
          </div>
        </div>
      </div>

      {/* Two separate honesty notices. The first is about whether the tilt reading means
          anything at all; the second is about the rotation, which is integrated and
          therefore always approximate. They are different claims and are not merged. */}
      {attitude && !attitude.reliable && (
        <div className="notice notice--warn">
          <span aria-hidden="true">▲</span> Attitude unreliable — {attitude.reason}
        </div>
      )}
      <p className="panel__footnote">
        Rotation about vertical is integrated from the gyro — relative, and drifts. No
        heading reference on this vehicle.
      </p>
    </Panel>
  )
}
