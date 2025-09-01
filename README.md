![스크린샷 2025-06-25 134455](https://github.com/user-attachments/assets/c1db44ad-d6b4-4733-b9cc-2d9e14a09823)

## Material Constant Buffers

Materials now keep CPU-side copies of their shader constant buffer data. The Inspector lists each reflected variable and lets you tweak values in real time. Changes are synchronized to the GPU through `ShaderPSO::UpdateVariable` and serialized with the material asset.

```cpp
// Update a single float in a constant buffer
material->m_shaderPSO->UpdateVariable("PerMaterial", "roughness", newValue);
```
