import { ContactShadows, Environment, Grid, Lightformer, OrbitControls } from '@react-three/drei';
import { Canvas, useFrame } from '@react-three/fiber';
import { useMemo, useRef } from 'react';
import * as THREE from 'three';
import type { SimulationSnapshot } from '../simulation-engine';
import { gearGeometry, helixGeometry } from './geometry';

/**
 * Read-only 3D view of the simulated desk.
 *
 * Every moving part is driven from the simulation snapshot each frame:
 * - tabletop / frame / inner column stages: column heights
 * - hex shafts, coupling, worm wheel, lead screws: shaft revolutions
 * - worm and motor rotor: motor revolutions
 * Nothing here integrates time on its own, so the picture cannot drift away
 * from the numbers shown in telemetry.
 *
 * Geometry follows the reference desk photos: single motor and gearbox at the
 * left column, a long hex shaft running inside the crossbeam channel, a
 * coupling with a white bearing block mid-span. Proportions are approximate
 * and the gearbox internals (shown as a worm stage) are representative only.
 */

export interface DeskSceneProps {
  getSnapshot: () => SimulationSnapshot;
  xray: boolean;
  view: 'overview' | 'drive';
}

const TAU = Math.PI * 2;
const COLUMN_X = 0.6;
const TOP_T = 0.025; // tabletop thickness
const SHAFT_DROP = TOP_T + 0.028; // shaft axis below tabletop surface
const SHAFT_R = 0.008;
const WHEEL_R = 0.026;
const WORM_R = 0.008;
const GEARBOX_X = -COLUMN_X + 0.1;
const OUTER_TOP = 0.62; // top of fixed lower column tube
const INNER_LEN = 0.63;

const mat = {
  frame: new THREE.MeshStandardMaterial({ color: '#2b2f37', metalness: 0.5, roughness: 0.5 }),
  wood: new THREE.MeshStandardMaterial({ color: '#5e412c', roughness: 0.7 }),
  steel: new THREE.MeshStandardMaterial({ color: '#c9ced6', metalness: 0.9, roughness: 0.25 }),
  brass: new THREE.MeshStandardMaterial({ color: '#c79a4b', metalness: 0.8, roughness: 0.3 }),
  white: new THREE.MeshStandardMaterial({ color: '#e9ece8', roughness: 0.6, transparent: true, opacity: 0.85 }),
  motor: new THREE.MeshStandardMaterial({ color: '#23252b', metalness: 0.4, roughness: 0.5 }),
  marker: new THREE.MeshStandardMaterial({ color: '#ff8a1f', emissive: '#ff6a00', emissiveIntensity: 0.6 }),
};

function useXrayMaterial(base: THREE.MeshStandardMaterial, xray: boolean, opacity = 0.18) {
  return useMemo(() => {
    const m = base.clone();
    m.transparent = xray;
    m.opacity = xray ? opacity : 1;
    m.depthWrite = !xray;
    return m;
  }, [base, xray, opacity]);
}

function Mechanism({ getSnapshot, xray }: Omit<DeskSceneProps, 'view'>) {
  const frame = useRef<THREE.Group>(null);
  const leftInner = useRef<THREE.Group>(null);
  const rightInner = useRef<THREE.Group>(null);
  const shafts = useRef<THREE.Group[]>([]);
  const worm = useRef<THREE.Group>(null);
  const screws = useRef<THREE.Group[]>([]);
  const glow = useRef<THREE.MeshStandardMaterial>(mat.marker.clone());

  const snap0 = getSnapshot();
  const gearRatio = snap0.profile.mechanical.gearRatio.value;
  const lead = snap0.profile.mechanical.travelPerShaftRevMm.value / 1000;
  const teeth = Math.min(120, Math.max(8, Math.round(gearRatio)));

  const wheelGeo = useMemo(() => gearGeometry(teeth, WHEEL_R - 0.003, WHEEL_R, 0.008, SHAFT_R * 1.02), [teeth]);
  const wormGeo = useMemo(
    // single-start worm: pitch equals the wheel's circular pitch, so one worm turn = one tooth
    () => helixGeometry(WORM_R, (TAU * WHEEL_R) / teeth, 0.04, 0.0018),
    [teeth],
  );
  const screwGeo = useMemo(() => helixGeometry(0.009, Math.max(0.002, lead), 0.55, 0.0016), [lead]);
  const hexGeo = useMemo(() => new THREE.CylinderGeometry(SHAFT_R, SHAFT_R, 1, 6), []);

  const columnMat = useXrayMaterial(mat.frame, xray);
  const housingMat = useXrayMaterial(mat.motor, true, xray ? 0.12 : 0.35);

  useFrame(() => {
    const s = getSnapshot();
    const m = s.mechanical;
    const h = m.heightMm / 1000;
    if (frame.current) frame.current.position.y = h;
    if (leftInner.current) leftInner.current.position.y = m.leftColumnHeightMm / 1000;
    if (rightInner.current) rightInner.current.position.y = m.rightColumnHeightMm / 1000;
    const shaftAngle = (m.shaftRevs % 1) * TAU;
    const motorAngle = (m.motorRevs % 1) * TAU;
    for (const g of shafts.current) if (g) g.rotation.x = shaftAngle;
    for (const g of screws.current) if (g) g.rotation.y = shaftAngle;
    if (worm.current) worm.current.rotation.z = motorAngle;
    const moving = Math.abs(m.shaftRpm) > 1e-6;
    glow.current.emissiveIntensity = moving ? 1.6 : 0.4;
  });

  const shaftLen = (x0: number, x1: number) => ({ x: (x0 + x1) / 2, len: x1 - x0 });
  const primary = shaftLen(-COLUMN_X, 0);
  const secondary = shaftLen(0, COLUMN_X);

  const column = (side: -1 | 1) => (
    <group key={side} position={[side * COLUMN_X, 0, 0]}>
      {/* foot */}
      <mesh material={mat.frame} position={[0, 0.02, 0]} castShadow>
        <boxGeometry args={[0.07, 0.04, 0.7]} />
      </mesh>
      {/* fixed outer tube */}
      <mesh material={columnMat} position={[0, (OUTER_TOP + 0.04) / 2, 0]} castShadow>
        <boxGeometry args={[0.08, OUTER_TOP - 0.04, 0.06]} />
      </mesh>
    </group>
  );

  const innerStage = (side: -1 | 1) => (
    // Positioned at the column's own height so a future sync error would be visible.
    <group key={side} ref={side < 0 ? leftInner : rightInner} position={[side * COLUMN_X, 0.72, 0]}>
      <mesh material={columnMat} position={[0, -TOP_T - INNER_LEN / 2, 0]} castShadow>
        <boxGeometry args={[0.07, INNER_LEN, 0.05]} />
      </mesh>
      {/* lead screw, rotated by the hex shaft through the column-top bevel stage */}
      <group
        ref={(g) => {
          if (g) screws.current[side < 0 ? 0 : 1] = g;
        }}
        position={[0, -TOP_T - 0.06 - 0.275, 0]}
      >
        <mesh material={mat.steel}>
          <cylinderGeometry args={[0.0075, 0.0075, 0.55, 12]} />
        </mesh>
        <mesh geometry={screwGeo} material={mat.brass} />
      </group>
    </group>
  );

  return (
    <group>
      {column(-1)}
      {column(1)}
      {innerStage(-1)}
      {innerStage(1)}

      <group ref={frame} position={[0, 0.72, 0]}>
        {/* tabletop */}
        <mesh material={mat.wood} position={[0, -TOP_T / 2, 0]} castShadow receiveShadow>
          <boxGeometry args={[1.4, TOP_T, 0.7]} />
        </mesh>
        {/* side brackets under the top */}
        {[-1, 1].map((side) => (
          <mesh key={side} material={mat.frame} position={[side * COLUMN_X, -TOP_T - 0.015, 0]}>
            <boxGeometry args={[0.05, 0.03, 0.6]} />
          </mesh>
        ))}
        {/* column-top drive housings (bevel stage) */}
        {[-1, 1].map((side) => (
          <mesh key={side} material={housingMat} position={[side * COLUMN_X, -SHAFT_DROP, 0]}>
            <boxGeometry args={[0.09, 0.05, 0.08]} />
          </mesh>
        ))}
        {/* crossbeam: C-channel open at the bottom, shaft runs inside */}
        <group position={[0, -TOP_T, 0]}>
          <mesh material={mat.frame} position={[0, -0.003, 0]}>
            <boxGeometry args={[2 * COLUMN_X - 0.1, 0.006, 0.06]} />
          </mesh>
          {[-1, 1].map((s) => (
            <mesh key={s} material={mat.frame} position={[0, -0.024, s * 0.029]}>
              <boxGeometry args={[2 * COLUMN_X - 0.1, 0.042, 0.003]} />
            </mesh>
          ))}
        </group>

        {/* hex shafts (primary: gearbox side, secondary: far column) */}
        {[primary, secondary].map((seg, i) => (
          <group
            key={i}
            position={[seg.x, -SHAFT_DROP, 0]}
            ref={(g) => {
              if (g) shafts.current[i] = g;
            }}
          >
            <mesh geometry={hexGeo} material={mat.steel} rotation={[0, 0, Math.PI / 2]} scale={[1, seg.len, 1]} />
            {/* orange stripe so rotation is easy to see */}
            <mesh material={glow.current} position={[0, SHAFT_R * 0.87, 0]}>
              <boxGeometry args={[seg.len * 0.98, 0.0012, 0.003]} />
            </mesh>
          </group>
        ))}

        {/* coupling joining the two shafts, inside a white bearing block */}
        <group position={[0, -SHAFT_DROP, 0]}>
          <group
            ref={(g) => {
              if (g) shafts.current[2] = g;
            }}
          >
            <mesh material={mat.brass} rotation={[0, 0, Math.PI / 2]}>
              <cylinderGeometry args={[0.011, 0.011, 0.05, 6]} />
            </mesh>
            <mesh material={glow.current} position={[0, 0.0098, 0]}>
              <boxGeometry args={[0.05, 0.0015, 0.004]} />
            </mesh>
          </group>
          <mesh material={mat.white}>
            <boxGeometry args={[0.09, 0.032, 0.05]} />
          </mesh>
        </group>

        {/* gearbox: worm on the motor shaft drives a wheel on the hex shaft */}
        <group position={[GEARBOX_X, -SHAFT_DROP, 0]}>
          <group
            ref={(g) => {
              if (g) shafts.current[3] = g;
            }}
          >
            <mesh geometry={wheelGeo} material={mat.brass} rotation={[0, Math.PI / 2, 0]} />
          </group>
          <group position={[0, -(WHEEL_R + WORM_R), 0]}>
            <group ref={worm} rotation={[Math.PI / 2, 0, 0]}>
              {/* worm axis runs along Z (towards the motor) */}
              <group rotation={[0, 0, 0]}>
                <mesh material={mat.steel}>
                  <cylinderGeometry args={[WORM_R - 0.0015, WORM_R - 0.0015, 0.05, 12]} />
                </mesh>
                <mesh geometry={wormGeo} material={mat.steel} />
              </group>
            </group>
          </group>
          {/* housing */}
          <mesh material={housingMat} position={[0, -0.012, 0.005]}>
            <boxGeometry args={[0.05, 0.08, 0.07]} />
          </mesh>
          {/* motor, perpendicular to the shaft like the reference desk */}
          <group position={[0, -(WHEEL_R + WORM_R), -0.03]}>
            <mesh material={mat.motor} rotation={[Math.PI / 2, 0, 0]} position={[0, 0, -0.075]} castShadow>
              <cylinderGeometry args={[0.024, 0.024, 0.15, 24]} />
            </mesh>
            <mesh material={mat.frame} rotation={[Math.PI / 2, 0, 0]} position={[0, 0, -0.155]}>
              <cylinderGeometry args={[0.018, 0.018, 0.012, 24]} />
            </mesh>
          </group>
        </group>
      </group>
    </group>
  );
}

function CameraRig({ view, getSnapshot }: Pick<DeskSceneProps, 'view' | 'getSnapshot'>) {
  const controls = useRef<React.ComponentRef<typeof OrbitControls>>(null);
  const last = useRef<string | null>(null);
  useFrame(({ camera }) => {
    if (last.current === view || !controls.current) return;
    last.current = view;
    const h = getSnapshot().mechanical.heightMm / 1000;
    if (view === 'overview') {
      camera.position.set(1.45, 0.5, 1.55);
      controls.current.target.set(0, h - 0.15, 0);
    } else {
      // From below and in front of the gearbox, looking up into the crossbeam.
      camera.position.set(-0.2, h - 0.24, 0.3);
      controls.current.target.set(-0.46, h - SHAFT_DROP - 0.02, -0.02);
    }
    controls.current.update();
  });
  return <OrbitControls ref={controls} makeDefault enableDamping minDistance={0.15} maxDistance={6} />;
}

export function DeskScene({ getSnapshot, xray, view }: DeskSceneProps) {
  return (
    <Canvas shadows dpr={[1, 2]} camera={{ fov: 40, near: 0.01, far: 50, position: [1.45, 0.5, 1.55] }}>
      <color attach="background" args={['#0f1218']} />
      <hemisphereLight args={['#c8d4ff', '#20160f', 0.9]} />
      <ambientLight intensity={0.25} />
      <directionalLight position={[2, 3, 2]} intensity={1.6} castShadow shadow-mapSize={[1024, 1024]} />
      <directionalLight position={[-2, 0.3, 1.5]} intensity={0.6} />
      {/* fill from below so the drive under the tabletop is readable */}
      <pointLight position={[0, 0.25, 0.6]} intensity={1.2} distance={3} />
      {/* local, offline studio environment for metal reflections (no HDR download) */}
      <Environment resolution={128} frames={1}>
        <Lightformer intensity={2} position={[0, 4, 0]} rotation-x={Math.PI / 2} scale={[6, 6, 1]} />
        <Lightformer intensity={1} position={[4, 1, 2]} rotation-y={-Math.PI / 2} scale={[4, 2, 1]} />
        <Lightformer intensity={0.8} position={[-4, 0.5, 2]} rotation-y={Math.PI / 2} scale={[4, 2, 1]} />
        <Lightformer intensity={0.6} position={[0, -2, 0]} rotation-x={-Math.PI / 2} scale={[6, 6, 1]} />
      </Environment>
      <Mechanism getSnapshot={getSnapshot} xray={xray} />
      <Grid
        args={[10, 10]}
        cellSize={0.1}
        sectionSize={0.5}
        cellColor="#252b36"
        sectionColor="#344055"
        fadeDistance={6}
        infiniteGrid
      />
      <ContactShadows position={[0, 0.001, 0]} opacity={0.5} scale={4} blur={2} far={1.5} />
      <CameraRig view={view} getSnapshot={getSnapshot} />
    </Canvas>
  );
}
