import * as THREE from 'three'

/**
 * Placeholder vehicle.
 *
 * Built procedurally rather than loaded from a GLB: nothing binary to inspect, no asset
 * to bundle, and it can be swapped for the real model with a loader call plus a scale
 * and axis fix.
 *
 * Local axes: the vehicle's long axis is +Y, nose upward. The accelerometer's body +Z
 * (which reads +1 g when the vehicle sits upright) maps onto this +Y — see
 * `bodyUpInModelSpace` in the attitude panel.
 *
 * The body carries a contrasting band and asymmetric fin colouring so rotation about
 * the long axis is actually visible. A featureless cylinder would spin invisibly.
 */

export interface RocketColors {
  body: string
  nose: string
  band: string
  fin: string
}

export const LIVE_COLORS: RocketColors = {
  body: '#3f3f46',
  nose: '#b91c1c',
  band: '#f4f4f5',
  fin: '#1d4ed8',
}

/** Desaturated set used when the attitude reading is not trustworthy. */
export const MUTED_COLORS: RocketColors = {
  body: '#a1a1aa',
  nose: '#c4c4c8',
  band: '#e4e4e7',
  fin: '#b4b4bb',
}

export interface Rocket {
  group: THREE.Group
  setColors: (colors: RocketColors) => void
  dispose: () => void
}

export function createRocket(): Rocket {
  const group = new THREE.Group()
  const materials: Record<keyof RocketColors, THREE.MeshStandardMaterial> = {
    body: new THREE.MeshStandardMaterial({ roughness: 0.55, metalness: 0.1 }),
    nose: new THREE.MeshStandardMaterial({ roughness: 0.45, metalness: 0.1 }),
    band: new THREE.MeshStandardMaterial({ roughness: 0.6, metalness: 0 }),
    fin: new THREE.MeshStandardMaterial({ roughness: 0.5, metalness: 0.1, side: THREE.DoubleSide }),
  }

  const geometries: THREE.BufferGeometry[] = []

  const addMesh = (
    geometry: THREE.BufferGeometry,
    material: THREE.Material,
    position: [number, number, number],
    rotation?: [number, number, number],
  ) => {
    geometries.push(geometry)
    const mesh = new THREE.Mesh(geometry, material)
    mesh.position.set(...position)
    if (rotation) mesh.rotation.set(...rotation)
    group.add(mesh)
    return mesh
  }

  const RADIUS = 0.3
  const BODY_LENGTH = 1.25

  addMesh(
    new THREE.CylinderGeometry(RADIUS, RADIUS, BODY_LENGTH, 28),
    materials.body,
    [0, 0, 0],
  )

  addMesh(
    new THREE.ConeGeometry(RADIUS, 0.55, 28),
    materials.nose,
    [0, BODY_LENGTH / 2 + 0.275, 0],
  )

  // Contrasting band — the roll cue.
  addMesh(
    new THREE.CylinderGeometry(RADIUS * 1.02, RADIUS * 1.02, 0.12, 28),
    materials.band,
    [0, BODY_LENGTH * 0.22, 0],
  )

  // Three fins at the base. One is coloured differently below, giving an unambiguous
  // reference mark for rotation.
  const finGeometry = new THREE.BoxGeometry(0.02, 0.42, 0.34)
  const finMaterials: THREE.MeshStandardMaterial[] = []
  for (let i = 0; i < 3; i++) {
    const angle = (i / 3) * Math.PI * 2
    const material = i === 0
      ? materials.fin
      : (materials.body.clone() as THREE.MeshStandardMaterial)
    if (i !== 0) finMaterials.push(material)
    const pivot = new THREE.Group()
    pivot.rotation.y = angle
    const mesh = new THREE.Mesh(finGeometry, material)
    mesh.position.set(0, -BODY_LENGTH / 2 + 0.14, RADIUS + 0.15)
    pivot.add(mesh)
    group.add(pivot)
  }
  geometries.push(finGeometry)

  // Base cap, so the underside is not an open tube when the vehicle is inverted.
  addMesh(
    new THREE.CircleGeometry(RADIUS, 28),
    materials.body,
    [0, -BODY_LENGTH / 2, 0],
    [Math.PI / 2, 0, 0],
  )

  const setColors = (colors: RocketColors) => {
    materials.body.color.set(colors.body)
    materials.nose.color.set(colors.nose)
    materials.band.color.set(colors.band)
    materials.fin.color.set(colors.fin)
    for (const material of finMaterials) material.color.set(colors.body)
  }

  setColors(LIVE_COLORS)

  return {
    group,
    setColors,
    dispose: () => {
      for (const geometry of geometries) geometry.dispose()
      for (const material of Object.values(materials)) material.dispose()
      for (const material of finMaterials) material.dispose()
    },
  }
}

/**
 * Horizon disc and vertical "up" reference.
 *
 * Tilt is very hard to read from a floating object with nothing to compare it against.
 * These give the eye a level plane and a true vertical.
 */
export function createReference(): { group: THREE.Group; dispose: () => void } {
  const group = new THREE.Group()

  const ring = new THREE.RingGeometry(0.95, 1.0, 48)
  const ringMaterial = new THREE.MeshBasicMaterial({
    color: '#a1a1aa',
    side: THREE.DoubleSide,
    transparent: true,
    opacity: 0.9,
  })
  const ringMesh = new THREE.Mesh(ring, ringMaterial)
  ringMesh.rotation.x = -Math.PI / 2
  ringMesh.position.y = -0.95
  group.add(ringMesh)

  const axis = new THREE.BufferGeometry().setFromPoints([
    new THREE.Vector3(0, -0.95, 0),
    new THREE.Vector3(0, 1.35, 0),
  ])
  const axisMaterial = new THREE.LineDashedMaterial({
    color: '#a1a1aa',
    dashSize: 0.08,
    gapSize: 0.06,
  })
  const axisLine = new THREE.Line(axis, axisMaterial)
  axisLine.computeLineDistances()
  group.add(axisLine)

  return {
    group,
    dispose: () => {
      ring.dispose()
      ringMaterial.dispose()
      axis.dispose()
      axisMaterial.dispose()
    },
  }
}
