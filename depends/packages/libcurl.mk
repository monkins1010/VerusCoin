package=libcurl
$(package)_version=7.67.0
$(package)_download_path=https://curl.haxx.se/download
$(package)_file_name=curl-$($(package)_version).tar.gz
$(package)_sha256_hash=52af3361cf806330b88b4fe6f483b6844209d47ae196ac46da4de59bb361ab02
$(package)_config_opts_linux=--disable-shared --enable-static --prefix=$(host_prefix) --host=$(HOST)
$(package)_config_opts_mingw32=--enable-mingw --disable-shared --enable-static --prefix=$(host_prefix) --host=x86_64-w64-mingw32
$(package)_config_opts_darwin=--disable-shared --enable-static --prefix=$(host_prefix)
$(package)_cflags_darwin=-mmacosx-version-min=10.9
$(package)_conf_tool=./configure

ifeq ($(build_os),darwin)
define $(package)_set_vars
  $(package)_build_env=MACOSX_DEPLOYMENT_TARGET="10.9"
endef
endif

define $(package)_config_cmds
  $($(package)_conf_tool) $($(package)_config_opts)
endef

ifeq ($(build_os),darwin)
define $(package)_build_cmds
  $(MAKE) CPPFLAGS='-fPIC' CFLAGS='-mmacosx-version-min=10.9'
endef
else
define $(package)_build_cmds
  $(MAKE)
endef
endif

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef
