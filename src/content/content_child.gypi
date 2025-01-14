# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'dependencies': [
    '../base/base.gyp:base',
    '../components/tracing.gyp:tracing',
    '../skia/skia.gyp:skia',
    '../ui/ui.gyp:ui',
    '../url/url.gyp:url_lib',
  ],
  'include_dirs': [
    '..',
  ],
  'export_dependent_settings': [
    '../base/base.gyp:base',
  ],
  'sources': [
    'child/android/child_jni_registrar.cc',
    'child/android/child_jni_registrar.h',
    'child/appcache_backend_proxy.cc',
    'child/appcache_backend_proxy.h',
    'child/appcache_dispatcher.cc',
    'child/appcache_dispatcher.h',
    'child/child_histogram_message_filter.cc',
    'child/child_histogram_message_filter.h',
    'child/child_process.cc',
    'child/child_process.h',
    'child/child_resource_message_filter.cc',
    'child/child_resource_message_filter.h',
    'child/child_thread.cc',
    'child/child_thread.h',
    'child/database_util.cc',
    'child/database_util.h',
    'child/db_message_filter.cc',
    'child/db_message_filter.h',
    'child/fileapi/file_system_dispatcher.cc',
    'child/fileapi/file_system_dispatcher.h',
    'child/fileapi/webfilesystem_callback_adapters.cc',
    'child/fileapi/webfilesystem_callback_adapters.h',
    'child/fileapi/webfilesystem_impl.cc',
    'child/fileapi/webfilesystem_impl.h',
    'child/fileapi/webfilewriter_impl.cc',
    'child/fileapi/webfilewriter_impl.h',
    'child/image_decoder.cc',
    'child/image_decoder.h',
    'child/indexed_db/indexed_db_dispatcher.cc',
    'child/indexed_db/indexed_db_dispatcher.h',
    'child/indexed_db/indexed_db_key_builders.cc',
    'child/indexed_db/indexed_db_key_builders.h',
    'child/indexed_db/indexed_db_message_filter.cc',
    'child/indexed_db/indexed_db_message_filter.h',
    'child/indexed_db/proxy_webidbcursor_impl.cc',
    'child/indexed_db/proxy_webidbcursor_impl.h',
    'child/indexed_db/proxy_webidbdatabase_impl.cc',
    'child/indexed_db/proxy_webidbdatabase_impl.h',
    'child/indexed_db/proxy_webidbfactory_impl.cc',
    'child/indexed_db/proxy_webidbfactory_impl.h',
    'child/npapi/np_channel_base.cc',
    'child/npapi/np_channel_base.h',
    'child/npapi/npobject_base.h',
    'child/npapi/npobject_proxy.cc',
    'child/npapi/npobject_proxy.h',
    'child/npapi/npobject_stub.cc',
    'child/npapi/npobject_stub.h',
    'child/npapi/npobject_util.cc',
    'child/npapi/npobject_util.h',
    'child/npapi/npruntime_util.cc',
    'child/npapi/npruntime_util.h',
    'child/npapi/plugin_host.cc',
    'child/npapi/plugin_host.h',
    'child/npapi/plugin_instance.cc',
    'child/npapi/plugin_instance.h',
    'child/npapi/plugin_instance_mac.mm',
    'child/npapi/plugin_lib.cc',
    'child/npapi/plugin_lib.h',
    'child/npapi/plugin_stream.cc',
    'child/npapi/plugin_stream.h',
    'child/npapi/plugin_stream_posix.cc',
    'child/npapi/plugin_stream_url.cc',
    'child/npapi/plugin_stream_url.h',
    'child/npapi/plugin_stream_win.cc',
    'child/npapi/plugin_string_stream.cc',
    'child/npapi/plugin_string_stream.h',
    'child/npapi/plugin_web_event_converter_mac.h',
    'child/npapi/plugin_web_event_converter_mac.mm',
    'child/npapi/webplugin.h',
    'child/npapi/webplugin_accelerated_surface_mac.h',
    'child/npapi/webplugin_delegate.h',
    'child/npapi/webplugin_delegate_impl.cc',
    'child/npapi/webplugin_delegate_impl.h',
    'child/npapi/webplugin_delegate_impl_android.cc',
    'child/npapi/webplugin_delegate_impl_aura.cc',
    'child/npapi/webplugin_delegate_impl_gtk.cc',
    'child/npapi/webplugin_delegate_impl_mac.mm',
    'child/npapi/webplugin_delegate_impl_win.cc',
    'child/npapi/webplugin_ime_win.cc',
    'child/npapi/webplugin_ime_win.h',
    'child/plugin_message_generator.cc',
    'child/plugin_message_generator.h',
    'child/plugin_messages.h',
    'child/plugin_param_traits.cc',
    'child/plugin_param_traits.h',
    'child/power_monitor_broadcast_source.cc',
    'child/power_monitor_broadcast_source.h',
    'child/quota_dispatcher.cc',
    'child/quota_dispatcher.h',
    'child/quota_message_filter.cc',
    'child/quota_message_filter.h',
    'child/request_extra_data.cc',
    'child/request_extra_data.h',
    'child/resource_dispatcher.cc',
    'child/resource_dispatcher.h',
    'child/runtime_features.cc',
    'child/runtime_features.h',
    'child/socket_stream_dispatcher.cc',
    'child/socket_stream_dispatcher.h',
    'child/thread_safe_sender.cc',
    'child/thread_safe_sender.h',
    'child/web_database_observer_impl.cc',
    'child/web_database_observer_impl.h',
    'child/webblobregistry_impl.cc',
    'child/webblobregistry_impl.h',
    'child/webkitplatformsupport_impl.cc',
    'child/webkitplatformsupport_impl.h',
    'child/webmessageportchannel_impl.cc',
    'child/webmessageportchannel_impl.h',
    'public/child/image_decoder_utils.h',
    'public/child/resource_dispatcher_delegate.h',
  ],
  'conditions': [
    ['OS=="android"', {
      'includes': [
        '../build/android/cpufeatures.gypi',
      ],
    }],
    ['enable_plugins==1', {
      'sources': [
        'child/browser_font_resource_trusted.cc',
        'child/browser_font_resource_trusted.h',
      ],
    }],
    ['OS=="ios"', {
      'sources/': [
        # iOS only needs a small portion of content; exclude all the
        # implementation, and re-include what is used.
        ['exclude', '\\.(cc|mm)$'],
      ],
    }, {  # OS!="ios"
      'dependencies': [
        '../third_party/WebKit/public/blink.gyp:blink',
        '../third_party/npapi/npapi.gyp:npapi',
        '../webkit/common/webkit_common.gyp:webkit_common',
        '../webkit/support/webkit_support.gyp:glue_child',
      ],
    }],
    ['use_aura==1', {
      'sources!': [
        'child/npapi/webplugin_delegate_impl_mac.mm',
      ],
    }],
    ['use_aura==1 and OS=="win"', {
      'sources!': [
        'child/npapi/webplugin_delegate_impl_aura.cc',
      ],
    }],
  ],
}
