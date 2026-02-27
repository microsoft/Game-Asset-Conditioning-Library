#-------------------------------------------------------------------------------------
# onnxExporter.py
#
# Game Asset Conditioning Library - Microsoft toolkit for game asset compression
#
# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.
#
#-------------------------------------------------------------------------------------

import os
import torch
import lpips
import onnx

# Get the directory where this script lives, then navigate to ThirdParty/models
script_dir = os.path.dirname(os.path.abspath(__file__))
models_dir = os.path.join(script_dir, "..", "..", "ThirdParty", "models")

# Ensure the models directory exists
os.makedirs(models_dir, exist_ok=True)

# Initialize LPIPS with VGG
loss_fn_vgg = lpips.LPIPS(net='vgg')

# Example inputs (normalized to [-1, 1])
example_input = torch.rand(1, 3, 64, 64) * 2 - 1
example_target = torch.rand(1, 3, 64, 64) * 2 - 1

vgg_output_path = os.path.join(models_dir, "vgg_model.onnx")
torch.onnx.export(
    loss_fn_vgg,
    (example_input, example_target),
    vgg_output_path,
    input_names=["input", "target"],
    output_names=["loss"],
    opset_version=18,
    dynamic_axes={
        "input": {2: "height", 3: "width"},
        "target": {2: "height", 3: "width"},
        "loss": {0: "batch"}
    }
)

# Remove external data by reloading and saving with all weights embedded
model = onnx.load(vgg_output_path, load_external_data=True)
onnx.save_model(model, vgg_output_path, save_as_external_data=False)

# Clean up any leftover data files
for f in os.listdir(models_dir):
    if f.startswith("vgg_loss") and not f.endswith(".onnx"):
        os.remove(os.path.join(models_dir, f))

print(f"Exported: {vgg_output_path}")

# Initialize LPIPS with AlexNet
loss_fn_alex = lpips.LPIPS(net='alex')

# Example inputs (normalized to [-1, 1])
example_input = torch.rand(1, 3, 64, 64) * 2 - 1
example_target = torch.rand(1, 3, 64, 64) * 2 - 1

lpips_output_path = os.path.join(models_dir, "lpips_model.onnx")
torch.onnx.export(
    loss_fn_alex,
    (example_input, example_target),
    lpips_output_path,
    input_names=["input", "target"],
    output_names=["loss"],
    opset_version=18,
    dynamic_axes={
        "input": {2: "height", 3: "width"},
        "target": {2: "height", 3: "width"},
        "loss": {0: "batch"}
    }
)

# Remove external data by reloading and saving with all weights embedded
model = onnx.load(lpips_output_path, load_external_data=True)
onnx.save_model(model, lpips_output_path, save_as_external_data=False)

# Clean up any leftover data files
for f in os.listdir(models_dir):
    if f.startswith("lpips_loss") and not f.endswith(".onnx"):
        os.remove(os.path.join(models_dir, f))

print(f"Exported: {lpips_output_path}")