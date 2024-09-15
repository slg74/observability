#!/bin/bash

sar -P ALL 1 86400 | sadf -g -- -P ALL > cpu_stats.svg
