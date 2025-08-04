#!/bin/bash
echo "🔨 Building Virtual Memory Manager Documentation..."

# Check for Doxygen
if ! command -v doxygen &> /dev/null; then
    echo "❌ Doxygen not found. Please install Doxygen."
    exit 1
fi

# Generate documentation
echo "📚 Generating documentation..."
doxygen Doxyfile_enhanced

# Copy assets
echo "📋 Copying assets..."
cp -r docs/assets/* documentation/html/ 2>/dev/null || true

# Verify Three.js assets
if [ ! -f "documentation/html/js/three.min.js" ]; then
    echo "📦 Downloading Three.js..."
    curl -s https://cdnjs.cloudflare.com/ajax/libs/three.js/r128/three.min.js -o documentation/html/js/three.min.js
fi

echo "✅ Documentation build complete!"
echo "🌐 Run './serve_docs.py' to view the documentation"
