/**
 * Command
 *
 * A deep class that encapsulates all undoable operations in the diagram editor.
 * This is intentionally a large class (1000-2000 lines) with a simple public interface.
 *
 * Design Philosophy (from software-design.md):
 * - Deep class: Simple interface (execute/undo/canExecute), complex internals
 * - Pull complexity downward: All operation logic centralized here
 * - Working code isn't enough: Memory-efficient undo (stores deltas, not full state)
 * - General-purpose: Single abstraction handles all operation types
 *
 * Why One Large Class (Not Classitis):
 * - Public interface is tiny: 3 methods
 * - Internal operation handlers are private (not exposed)
 * - Alternative (many Command subclasses) creates shallow class explosion
 * - This approach: 1 deep class vs 20+ shallow classes
 *
 * Memory Efficiency:
 * - Stores minimal data for undo (deltas, not snapshots)
 * - Example: Drag operation stores deltaX/deltaY (~40 bytes)
 * - vs State snapshot approach (20KB+ per operation)
 * - ~500× more memory efficient
 *
 * Command Lifecycle:
 * 1. Created with operation type + data
 * 2. execute() called - performs operation, stores undo data
 * 3. Pushed to CommandHistory undo stack
 * 4. If user undoes: undo() called - reverses operation
 * 5. If user redoes: execute() called again
 */
import { Selection } from '../selection/Selection.js';
import { TextContainer } from '../tools/TextContainer.js';
import { ObjectManager } from '../tools/ObjectManager.js';
import { DebugManager } from '../ui/DebugManager.js';
import { VisualPosition } from '../text/TextStructures.js';
import { ShapeFactory } from '../tools/ShapeFactory.js';
import { Rectangle } from '../shapes/index.js';

/**
 * All operation types supported by the command system.
 * This will grow as new features are added (resize, rotate, align, etc.)
 */
export type OperationType =
    | 'drag'
    | 'insert-character'
    | 'delete-backward'
    | 'delete-forward'
    | 'insert-hard-break'
    | 'move-cursor-left'
    | 'move-cursor-right'
    | 'move-cursor-up'
    | 'move-cursor-down'
    | 'move-cursor-home'
    | 'move-cursor-end'
    | 'group'
    | 'ungroup'
    | 'delete-objects'
    | 'update-style'
    | 'create-shape'
    | 'delete-shape';

/**
 * Minimal data storage for each operation type.
 * Only stores what's needed for undo - no redundant state.
 */
export interface CommandData {
    // Drag: just delta, not full bounds
    drag?: {
        deltaX: number;
        deltaY: number;
    };

    // Insert character: the character + cursor positions before insertion
    insertCharacter?: {
        char: string;
        // Stored during execute() for undo
        cursorPositions?: Map<string, VisualPosition>;
    };

    // Delete operations: store deleted content for undo
    deleteBackward?: {
        // Stored during execute()
        deletedContent?: Map<string, { char: string; cursorPos: VisualPosition }>;
    };

    deleteForward?: {
        // Stored during execute()
        deletedContent?: Map<string, { char: string; cursorPos: VisualPosition }>;
    };

    // Hard break: store cursor positions
    insertHardBreak?: {
        cursorPositions?: Map<string, VisualPosition>;
    };

    // Cursor movements: store old positions
    cursorMovement?: {
        oldPositions?: Map<string, VisualPosition>;
    };

    // Group: member IDs and group ID
    group?: {
        memberIds: string[];
        groupId: string;
        objectManager?: ObjectManager; // Injected during execute
    };

    // Ungroup: restore members
    ungroup?: {
        groupId: string;
        memberIds: string[];
        objectManager?: ObjectManager; // Injected during execute
    };

    // Delete objects: store for undo
    deleteObjects?: {
        objectManager?: ObjectManager;
        // Stored during execute()
        deletedObjects?: Array<{ object: any; index: number }>;
    };

    // Update style: apply delta changes to selected objects
    updateStyle?: {
        newStyles: Partial<any>;  // The delta to apply (only changed properties)
        // Stored during execute() for undo
        previousStyles?: Map<string, any>;  // objectId → old style
    };

    // Create shape: type, bounds, style
    createShape?: {
        type: 'rectangle' | 'text';
        bounds: Rectangle;
        style: any;
        // Stored during execute() for undo:
        createdObjectId?: string;
    };

    // Delete shape: object ID
    deleteShape?: {
        objectId: string;
        // Stored during execute() for undo:
        deletedObject?: any;
        deletedState?: any;
        deletedIndex?: number;
    };
}

export class Command {
    private operation: OperationType;
    private target: Selection | null;
    private data: CommandData;
    private debugManager: DebugManager;

    constructor(
        operation: OperationType,
        target: Selection | null,
        data: CommandData = {},
        private shapeFactory?: ShapeFactory,
        private objectManager?: ObjectManager
    ) {
        this.operation = operation;
        this.target = target;
        this.data = data;
        this.debugManager = DebugManager.getInstance();
    }

    // === PUBLIC INTERFACE (Simple - only 3 methods) ===

    /**
     * Get the operation type for this command.
     */
    public getOperation(): OperationType {
        return this.operation;
    }

    /**
     * Execute this command.
     * Performs the operation and stores undo data.
     */
    public execute(): void {
        this.debugManager.log(`Executing command: ${this.operation}`, 'Command', 'command:execute');

        switch (this.operation) {
            case 'drag':
                this.executeDrag();
                break;
            case 'insert-character':
                this.executeInsertCharacter();
                break;
            case 'delete-backward':
                this.executeDeleteBackward();
                break;
            case 'delete-forward':
                this.executeDeleteForward();
                break;
            case 'insert-hard-break':
                this.executeInsertHardBreak();
                break;
            case 'move-cursor-left':
                this.executeCursorMovement(() => this.target?.moveCursorLeft());
                break;
            case 'move-cursor-right':
                this.executeCursorMovement(() => this.target?.moveCursorRight());
                break;
            case 'move-cursor-up':
                this.executeCursorMovement(() => this.target?.moveCursorUp());
                break;
            case 'move-cursor-down':
                this.executeCursorMovement(() => this.target?.moveCursorDown());
                break;
            case 'move-cursor-home':
                this.executeCursorMovement(() => this.target?.moveCursorHome());
                break;
            case 'move-cursor-end':
                this.executeCursorMovement(() => this.target?.moveCursorEnd());
                break;
            case 'group':
                this.executeGroup();
                break;
            case 'ungroup':
                this.executeUngroup();
                break;
            case 'delete-objects':
                this.executeDeleteObjects();
                break;
            case 'update-style':
                this.executeUpdateStyle();
                break;
            case 'create-shape':
                this.executeCreateShape();
                break;
            case 'delete-shape':
                this.executeDeleteShape();
                break;
            default:
                throw new Error(`Unknown operation: ${this.operation}`);
        }
    }

    /**
     * Undo this command.
     * Reverses the operation using stored undo data.
     */
    public undo(): void {
        this.debugManager.log(`Undoing command: ${this.operation}`, 'Command', 'command:undo');

        switch (this.operation) {
            case 'drag':
                this.undoDrag();
                break;
            case 'insert-character':
                this.undoInsertCharacter();
                break;
            case 'delete-backward':
                this.undoDeleteBackward();
                break;
            case 'delete-forward':
                this.undoDeleteForward();
                break;
            case 'insert-hard-break':
                this.undoInsertHardBreak();
                break;
            case 'move-cursor-left':
            case 'move-cursor-right':
            case 'move-cursor-up':
            case 'move-cursor-down':
            case 'move-cursor-home':
            case 'move-cursor-end':
                this.undoCursorMovement();
                break;
            case 'group':
                this.undoGroup();
                break;
            case 'ungroup':
                this.undoUngroup();
                break;
            case 'delete-objects':
                this.undoDeleteObjects();
                break;
            case 'update-style':
                this.undoUpdateStyle();
                break;
            case 'create-shape':
                this.undoCreateShape();
                break;
            case 'delete-shape':
                this.undoDeleteShape();
                break;
            default:
                throw new Error(`Unknown operation: ${this.operation}`);
        }
    }

    /**
     * Check if this command can be executed.
     * Used for validation before execution.
     */
    public canExecute(): boolean {
        switch (this.operation) {
            case 'insert-character':
            case 'delete-backward':
            case 'delete-forward':
            case 'insert-hard-break':
            case 'move-cursor-left':
            case 'move-cursor-right':
            case 'move-cursor-up':
            case 'move-cursor-down':
            case 'move-cursor-home':
            case 'move-cursor-end':
                return this.target !== null && this.target.canReceiveText();
            case 'group':
                return this.target !== null && this.target.getMemberCount() >= 2;
            case 'ungroup':
                return this.target !== null && this.target.isGroup();
            default:
                return true;
        }
    }

    // === INTERNAL OPERATION HANDLERS (Complex, but hidden) ===

    // --- Drag Operations ---

    private executeDrag(): void {
        if (!this.data.drag || !this.target) return;

        const currentBounds = this.target.getBounds();
        const newBounds = {
            ...currentBounds,
            x: currentBounds.x + this.data.drag.deltaX,
            y: currentBounds.y + this.data.drag.deltaY
        };

        this.target.setBounds(newBounds);
    }

    private undoDrag(): void {
        if (!this.data.drag || !this.target) return;

        // Inverse delta
        const currentBounds = this.target.getBounds();
        const newBounds = {
            ...currentBounds,
            x: currentBounds.x - this.data.drag.deltaX,
            y: currentBounds.y - this.data.drag.deltaY
        };

        this.target.setBounds(newBounds);
    }

    // --- Text Insertion ---

    private executeInsertCharacter(): void {
        if (!this.data.insertCharacter || !this.target) return;

        // Store cursor positions BEFORE insertion (for undo)
        this.data.insertCharacter.cursorPositions = new Map();
        for (const member of this.target.getMembers()) {
            if (member instanceof TextContainer) {
                const pos = member.getVisualCursor();
                this.data.insertCharacter.cursorPositions.set(
                    member.getId(),
                    { ...pos }
                );
            }
        }

        // Execute insertion
        this.target.insertCharacter(this.data.insertCharacter.char);
    }

    private undoInsertCharacter(): void {
        if (!this.data.insertCharacter?.cursorPositions || !this.target) return;

        // For each TextContainer, restore cursor and delete the inserted character
        for (const member of this.target.getMembers()) {
            if (member instanceof TextContainer) {
                const oldPos = this.data.insertCharacter.cursorPositions.get(
                    member.getId()
                );
                if (oldPos) {
                    // Move cursor forward to where character was inserted, then delete
                    member.deleteBackward();
                }
            }
        }
    }

    // --- Delete Operations ---

    private executeDeleteBackward(): void {
        // TODO: Store deleted content for proper undo
        // For now, just execute the operation
        if (!this.target) return;
        this.target.deleteBackward();
    }

    private undoDeleteBackward(): void {
        // TODO: Restore deleted content
        // For now, no-op (delete operations aren't undoable yet)
    }

    private executeDeleteForward(): void {
        // TODO: Store deleted content for proper undo
        if (!this.target) return;
        this.target.deleteForward();
    }

    private undoDeleteForward(): void {
        // TODO: Restore deleted content
    }

    // --- Hard Break ---

    private executeInsertHardBreak(): void {
        if (!this.target) return;

        if (!this.data.insertHardBreak) {
            this.data.insertHardBreak = {};
        }

        // Store cursor positions before insertion
        this.data.insertHardBreak.cursorPositions = new Map();
        for (const member of this.target.getMembers()) {
            if (member instanceof TextContainer) {
                const pos = member.getVisualCursor();
                this.data.insertHardBreak.cursorPositions.set(
                    member.getId(),
                    { ...pos }
                );
            }
        }

        this.target.insertHardBreak();
    }

    private undoInsertHardBreak(): void {
        // TODO: Delete the inserted newline
        // Similar to undoInsertCharacter
    }

    // --- Cursor Movement ---

    private executeCursorMovement(movementFn: () => void): void {
        if (!this.target) return;

        if (!this.data.cursorMovement) {
            this.data.cursorMovement = {};
        }

        // Store old cursor positions
        this.data.cursorMovement.oldPositions = new Map();
        for (const member of this.target.getMembers()) {
            if (member instanceof TextContainer) {
                const pos = member.getVisualCursor();
                this.data.cursorMovement.oldPositions.set(
                    member.getId(),
                    { ...pos }
                );
            }
        }

        // Execute movement
        movementFn();
    }

    private undoCursorMovement(): void {
        // TODO: Restore cursor positions
        // Requires TextContainer.setCursor() method
    }

    // --- Group Operations ---

    private executeGroup(): void {
        if (!this.data.group?.objectManager || !this.target) return;

        this.target.makePermanent();

        // Remove members from ObjectManager, add Group
        const objectManager = this.data.group.objectManager;
        for (const member of this.target.getMembers()) {
            objectManager.removeObject(member);
        }
        objectManager.addObject(this.target);

        this.debugManager.log(`Grouped ${this.target.getMemberCount()} objects`, 'Command', 'command:execute');
    }

    private undoGroup(): void {
        if (!this.data.group?.objectManager || !this.target) return;

        const objectManager = this.data.group.objectManager;

        // Remove group, restore members
        objectManager.removeObject(this.target);
        const members = this.target.ungroup();
        for (const member of members) {
            objectManager.addObject(member);
        }

        this.debugManager.log(`Ungrouped ${members.size} objects`, 'Command', 'command:undo');
    }

    // --- Ungroup Operations ---

    private executeUngroup(): void {
        if (!this.data.ungroup?.objectManager || !this.target) return;

        const objectManager = this.data.ungroup.objectManager;

        // Remove group, restore members
        objectManager.removeObject(this.target);
        const members = this.target.ungroup();
        for (const member of members) {
            objectManager.addObject(member);
        }
    }

    private undoUngroup(): void {
        if (!this.data.ungroup?.objectManager || !this.target) return;

        const objectManager = this.data.ungroup.objectManager;

        // Restore group
        this.target.makePermanent();
        for (const member of this.target.getMembers()) {
            objectManager.removeObject(member);
        }
        objectManager.addObject(this.target);
    }

    // --- Delete Objects ---

    private executeDeleteObjects(): void {
        // TODO: Store deleted objects for undo
        // Requires ObjectManager integration
    }

    private undoDeleteObjects(): void {
        // TODO: Restore deleted objects
    }

    // --- Update Style ---

    private executeUpdateStyle(): void {
        if (!this.data.updateStyle?.newStyles || !this.target) {
            this.debugManager.log('executeUpdateStyle: No new styles provided or no target', 'Command', 'command:execute');
            return;
        }

        this.debugManager.log(`executeUpdateStyle: Applying ${JSON.stringify(this.data.updateStyle.newStyles)}`, 'Command', 'command:execute');

        // Apply delta to all selected objects via Selection.setStyle()
        const previousStyles = this.target.setStyle(this.data.updateStyle.newStyles);

        // Store previous styles for undo (only on first execution)
        if (!this.data.updateStyle.previousStyles) {
            this.data.updateStyle.previousStyles = previousStyles;
            this.debugManager.log(`executeUpdateStyle: Stored ${previousStyles.size} previous styles for undo`, 'Command', 'command:execute');
        }
    }

    private undoUpdateStyle(): void {
        if (!this.data.updateStyle?.previousStyles || !this.target) {
            this.debugManager.log('undoUpdateStyle: No previous styles to restore or no target', 'Command', 'command:undo');
            return;
        }

        this.debugManager.log(`undoUpdateStyle: Restoring ${this.data.updateStyle.previousStyles.size} previous styles`, 'Command', 'command:undo');

        // Restore each object's previous style (full replacement, not delta)
        for (const member of this.target.getMembers()) {
            if ('getId' in member && typeof member.getId === 'function') {
                const memberId = member.getId();
                const previousStyle = this.data.updateStyle.previousStyles.get(memberId);
                if (previousStyle && 'setStyle' in member && typeof member.setStyle === 'function') {
                    this.debugManager.log(`Restoring style for ${memberId}: ${JSON.stringify(previousStyle)}`, 'Command', 'command:undo');
                    member.setStyle(previousStyle);
                }
            }
        }

        this.debugManager.log('Style restoration complete', 'Command', 'command:undo');
    }

    // --- Create Shape Operations ---

    private executeCreateShape(): void {
        if (!this.data.createShape || !this.shapeFactory || !this.objectManager) {
            this.debugManager.log('executeCreateShape: Missing dependencies', 'Command', 'command:execute');
            return;
        }

        const { type, bounds, style } = this.data.createShape;

        this.debugManager.log(`Creating ${type} shape at ${JSON.stringify(bounds)}`, 'Command', 'command:execute');

        // Create via factory (handles all subsystem registration)
        const object = this.shapeFactory.createShape(type, bounds, style);

        // Store object ID for undo
        if ('getId' in object && typeof object.getId === 'function') {
            this.data.createShape.createdObjectId = object.getId();
            this.debugManager.log(`Shape created with ID: ${this.data.createShape.createdObjectId}`, 'Command', 'command:execute');
        }

        // Select the newly created object
        this.objectManager.selectOnly(object);
    }

    private undoCreateShape(): void {
        if (!this.data.createShape?.createdObjectId || !this.shapeFactory) {
            this.debugManager.log('undoCreateShape: Missing object ID or factory', 'Command', 'command:undo');
            return;
        }

        this.debugManager.log(`Undoing creation of ${this.data.createShape.createdObjectId}`, 'Command', 'command:undo');

        // Remove from all subsystems via factory
        this.shapeFactory.removeShape(this.data.createShape.createdObjectId);
    }

    // --- Delete Shape Operations ---

    private executeDeleteShape(): void {
        // TODO: Implement if needed (for completeness)
        this.debugManager.log('executeDeleteShape: Not yet implemented', 'Command', 'command:execute');
    }

    private undoDeleteShape(): void {
        // TODO: Implement if needed (for completeness)
        this.debugManager.log('undoDeleteShape: Not yet implemented', 'Command', 'command:undo');
    }
}
