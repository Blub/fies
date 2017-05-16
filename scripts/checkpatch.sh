#!/bin/sh

dir="$(dirname "$0")"

exec "$dir/style-check.pl" --patch "$@"
