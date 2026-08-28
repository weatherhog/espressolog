import React from "react";
import { createRoot } from "react-dom/client";
import App from "./App.jsx";

const style = document.createElement("style");
style.textContent = `
  * { box-sizing: border-box; margin: 0; }
  html, body, #root { height: 100%; }
  button { -webkit-tap-highlight-color: transparent; }
`;
document.head.appendChild(style);

createRoot(document.getElementById("root")).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>
);
