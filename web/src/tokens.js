// Design tokens from the prototype: strip-chart recorder — pen on
// measurement paper. Light on purpose: read at arm's length in a kitchen.
export const T = {
  paper: "#F2F3EE",
  panel: "#FBFBF8",
  gridFine: "#E8CFC4",
  gridBold: "#D9A992",
  ink: "#1B2430",
  inkSoft: "#6B7480",
  trace: "#17497A",
  flow: "#7A8B3F",
  alert: "#A3341F",
  hair: "#D5D6CE",
};

export const MONO = "'SF Mono', 'DejaVu Sans Mono', Menlo, Consolas, monospace";
export const SANS = "'Avenir Next', 'Segoe UI', system-ui, sans-serif";

export const paperBg = {
  backgroundColor: T.paper,
  backgroundImage: `
    repeating-linear-gradient(0deg,  ${T.gridFine} 0 1px, transparent 1px 10px),
    repeating-linear-gradient(90deg, ${T.gridFine} 0 1px, transparent 1px 10px),
    repeating-linear-gradient(0deg,  ${T.gridBold} 0 1px, transparent 1px 50px),
    repeating-linear-gradient(90deg, ${T.gridBold} 0 1px, transparent 1px 50px)`,
};

// Overlay series colors, assigned per shot id on selection and kept stable
// while selected (color follows the shot, never its position in the list).
export const SERIES = ["#17497A", "#7A8B3F", "#A3341F", "#6E4E8E"];
