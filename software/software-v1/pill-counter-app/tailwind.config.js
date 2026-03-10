// tailwind.config.js
// This tells Tailwind which files to scan for class names.
// If a file isn't listed here, Tailwind won't include its styles in the final build.

/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,jsx}",
  ],
  theme: {
    extend: {},
  },
  plugins: [],
}

