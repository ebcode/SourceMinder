export function createStateMachine(transitions, initial) {
    var state = initial;
    return new Proxy({}, {
        get(_, prop) {
            if (prop === 'state') return state;
            if ((transitions[state] || []).includes(prop))
                return function() { state = prop; };
            return undefined;
        }
    });
}
