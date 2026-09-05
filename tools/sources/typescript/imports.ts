// Import forms. Each should produce an IMP row whose clue is the module path.

// Default import
import Selection from '../selection/Selection.js';

// Named import
import { TextContainer } from '../tools/TextContainer.js';

// Several names from one module
import { ObjectManager, ShapeFactory, DebugManager } from '../tools/managers.js';

// Renamed import
import { VisualPosition as Position } from '../text/TextStructures.js';

// Default plus named, together
import Rectangle, { Circle } from '../shapes/index.js';

// Namespace import
import * as geometry from '../shapes/geometry.js';

// Bare module name, no relative path
import { readFile } from 'node:fs/promises';

// Side-effect only, no bindings
import '../styles/editor.css';

// Type-only import
import type { CommandData } from '../commands/types.js';

export function use(): void {
  console.log(Selection, TextContainer, ObjectManager, ShapeFactory, DebugManager);
  console.log(Position, Rectangle, Circle, geometry, readFile);
}
