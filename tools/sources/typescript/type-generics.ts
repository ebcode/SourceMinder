// Type annotation forms, especially generics carrying commas.

export type OperationType = 'move' | 'resize' | 'delete';

export class TypeShapes {
  // any
  payload: any;

  // union of literals
  kind: OperationType;

  // generic with one type argument
  deletedObjects: Array<object>;

  // generic with two type arguments -- the comma and space sit inside one
  // column value, which is what breaks a parser that splits on punctuation
  cursorPositions: Map<string, VisualPosition>;

  // nested generic, commas at two depths
  buckets: Map<string, Array<number>>;

  // function type
  onDone: () => void;

  // function type taking arguments
  compare: (a: number, b: number) => boolean;

  // array shorthand
  labels: string[];

  // optional and readonly
  readonly id?: string;

  constructor() {
    this.payload = null;
    this.kind = 'move';
    this.deletedObjects = [];
    this.cursorPositions = new Map();
    this.buckets = new Map();
    this.onDone = () => {};
    this.compare = (a: number, b: number) => a < b;
    this.labels = [];
  }
}

export interface VisualPosition {
  x: number;
  y: number;
}

// Generic function with a constrained parameter
export function firstOf<T extends object>(items: Array<T>): T | undefined {
  return items[0];
}
