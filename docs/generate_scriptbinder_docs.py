"""Compatibility entry point for the current, source-checked API reference.

The retired ScriptBinder directory is no longer the API authority. The curated
reference lives in docs/api/reference.json and is reconciled against master.
"""
import pathlib
import runpy

if __name__ == '__main__':
    root = pathlib.Path(__file__).resolve().parents[1]
    runpy.run_path(str(root / 'Tools/docs/update-api-reference.py'), run_name='__main__')
