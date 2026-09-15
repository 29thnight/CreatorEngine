import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import App, { applyPageClass } from './App.jsx';
import './styles.css';

applyPageClass();

createRoot(document.getElementById('root')).render(
  <StrictMode>
    <App />
  </StrictMode>
);
