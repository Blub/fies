srcdir="$(dirname "$(readlink -f $0)")"
makfile="${srcdir}/config.mak"
confhdr="${srcdir}/config.h"
conflog="${srcdir}/config.log"

rm -f "$conflog"

OSNAME="$(uname -s)"

log() {
  echo "$@" >> "$conflog"
}

logrun() {
  echo "\$ $*" >> "$conflog"
  "$@" >> "$conflog" 2>&1
}

mak() {
  echo "$@" >> "$makfile"
}

cdefine() {
  echo >> "$confhdr"
  case "$2" in
    1|[Yy])
      echo "#define $1" >> "$confhdr"
      ;;
    *)
      echo "/* #undef $1 */" >> "$confhdr"
      ;;
  esac
}

confdef() {
  fetchvar "$1"
  cdefine "$1" "$fetched_var"
  addvar "$1"
}

subst() {
  c_subst_var="$1"; shift
  mak "$c_subst_var = $@"
}

die() {
  echo "$@" >&2
  exit 1
}

must() {
  "$@" || die failed
}

echo "ROOTDIR = ${srcdir}" > "$makfile"
echo "/* created by ./configure */" > "$confhdr"

setbyname() {
  setbyname_value="$2"
  eval "$1=\"\$setbyname_value\""
}

getbyname() {
  eval "getvar=\"\$$1\""
}

ifenabled() {
  ifenabled_name="$1"
  getbyname "$ifenabled_name"
  case "$getvar" in
    1|[Yy][Ee][Ss]|[Oo][Nn]|[Tt][Rr][Uu][Ee])
      shift
      "$@" || die 'failed'
      setbyname "$ifenabled_name" "enabled"
      ;;
    ?*)
      setbyname "$ifenabled_name" "disabled"
      return 1
      ;;
    *)
      shift
      if "$@"; then
        setbyname "$ifenabled_name" "auto enabled"
        return 0
      else
        setbyname "$ifenabled_name" "auto disabled"
        return 1
      fi
      ;;
  esac
}

checkprog() {
  what="$1"; shift
  var="$1"; shift

  echo -n "Checking for ${what}... "
  while [ $# -gt 0 ]; do
    prog="$1"; shift
    if which "$prog" >/dev/null 2>&1; then
      setvars_prog="$(which "$prog")"
      eval "${var}=\"\${setvars_prog}\""
      addvar "$var"
      echo "$setvars_prog"
      return
    fi
  done
  echo ' missing'
  return 1
}

addvar() {
  substvars="$substvars $*"
}

substall() {
  for i in ${substvars}; do
    eval "subst $i \"\${$i}\""
  done
}

cdefineall() {
  for i in ${cdefinevars}; do
    eval "cdefine $i \"\${$i}\""
  done
}

fetchvar() {
	eval "fetched_var=\"\${${1}}\""
}

fetch_pkgvars() {
  eval "fetched_cflags=\"\${${1}_CFLAGS}\""
  eval "fetched_libs=\"\${${1}_LIBS}\""
}

setvars() {
  setvars_cflags="$2"
  setvars_libs="$3"
  addvar "${1}_CFLAGS" "${1}_LIBS"
  eval "${1}_CFLAGS=\"\${setvars_cflags}\""
  eval "${1}_LIBS=\"\${setvars_libs}\""
}

checkfor() {
  what="$1"
  pre="$2"
  pkg="$3"
  prog="$4"
  echo -n "Checking for ${what}... "
  fetch_pkgvars "$pre"
  if [ "x${fetched_cflags}${fetched_libs}" != x ]; then
    setvars "$pre" "$fetched_cflags" "$fetched_libs"
    echo 'OK'
    return 0
  fi
  if [ "x${pkg}${prog}" = x ]; then
    die "Internal error (neither prog nor pkgname defined)"
  fi
  if [ "x$pkg" != "x" ]; then
    if pkg-config --exists "$pkg" ; then
      setvars "$pre" \
        "$(pkg-config --cflags "$pkg")" \
        "$(pkg-config --libs "$pkg")"
      echo 'OK'
      return 0
    fi
  fi
  if [ "x${prog}" != x ]; then
    if which "$prog" >/dev/null ; then
      setvars "$pre" \
        "$("$prog" --cflags)" \
        "$("$prog" --libs)"
      echo 'OK'
      return 0
    fi
  fi
  echo 'missing'
  return 1
}

trycc() {
  try_cflags="-std=c11 $1"
  try_libs="$2"
  try_code="$3"
  try_headers="$4"
  try_execvar="$5"
  for try_hdr in $try_headers; do
    echo "#include <${try_hdr}>"
  done >.cfgtest.c
  echo >>.cfgtest.c "$try_code"
  log '--- Trying to compile the following program ---'
  cat .cfgtest.c >>"$conflog"
  log ---
  logrun $CC $try_cflags -o .cfgtest.x .cfgtest.c $try_libs
  trycc_result=$?
  log '--- End of compilation unit ---'
  if [ "x$try_execvar" != x ]; then
    eval "$try_execvar=\"\$(./.cfgtest.x)\""
  fi
  rm -f .cfgtest.c .cfgtest.x
  return $trycc_result
}
