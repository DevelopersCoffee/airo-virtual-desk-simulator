import * as THREE from 'three';

/** Flat spur-style gear outline, extruded along +Z and centred on the origin. */
export function gearGeometry(teeth: number, rootRadius: number, tipRadius: number, width: number, boreRadius = 0) {
  const shape = new THREE.Shape();
  const step = (Math.PI * 2) / teeth;
  for (let i = 0; i < teeth; i++) {
    const a = i * step;
    const pts: [number, number][] = [
      [rootRadius, a],
      [tipRadius, a + step * 0.2],
      [tipRadius, a + step * 0.45],
      [rootRadius, a + step * 0.65],
    ];
    pts.forEach(([r, ang], j) => {
      const x = Math.cos(ang) * r;
      const y = Math.sin(ang) * r;
      if (i === 0 && j === 0) shape.moveTo(x, y);
      else shape.lineTo(x, y);
    });
  }
  shape.closePath();
  if (boreRadius > 0) {
    const hole = new THREE.Path();
    // Hexagonal bore that matches the hex shaft.
    for (let k = 0; k <= 6; k++) {
      const ang = (k / 6) * Math.PI * 2;
      const x = Math.cos(ang) * boreRadius;
      const y = Math.sin(ang) * boreRadius;
      if (k === 0) hole.moveTo(x, y);
      else hole.lineTo(x, y);
    }
    shape.holes.push(hole);
  }
  const g = new THREE.ExtrudeGeometry(shape, { depth: width, bevelEnabled: false, curveSegments: 4 });
  g.translate(0, 0, -width / 2);
  return g;
}

/** Helical thread running along +Y, used for the worm and the lead screws. */
export function helixGeometry(radius: number, pitch: number, length: number, tubeRadius: number) {
  const turns = length / pitch;
  const segments = Math.max(32, Math.ceil(turns * 24));
  class Helix extends THREE.Curve<THREE.Vector3> {
    public constructor() {
      super();
    }
    override getPoint(t: number, target = new THREE.Vector3()) {
      const ang = t * turns * Math.PI * 2;
      return target.set(Math.cos(ang) * radius, t * length - length / 2, Math.sin(ang) * radius);
    }
  }
  return new THREE.TubeGeometry(new Helix(), segments, tubeRadius, 6, false);
}
