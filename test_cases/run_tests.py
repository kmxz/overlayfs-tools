#!/usr/bin/env python3

import subprocess
import sys

def run_command(cmd):
    try:
        subprocess.run(cmd, shell=True, check=True)
    except subprocess.CalledProcessError as e:
        print(f"Command '{cmd}' failed with return code {e.returncode}")
        sys.exit(e.returncode)

# Define the commands
commands = [
    'ninja clean.tests',
    'ninja permanent',
    'ninja changes',
    'ninja diff.out',
    'ninja verbose.out',
    'ninja overlayed',
    'ninja brief.expected',
    'ninja brief.out'
]

# Run the commands
for cmd in commands:
    run_command(cmd)

# Perform the diff checks
run_command('diff -u ../test_cases/diff.saved diff.out')
run_command('diff -u ../test_cases/verbose.saved verbose.out')
run_command('diff -u brief.expected brief.out')
