/**
 * PBS nbdkit plugin: C version
 * Copyright (C) 2025  Michael Ablassmeier <abi@grinser.de>
 **/
#define _XOPEN_SOURCE 700
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define NBDKIT_API_VERSION 2
#include "proxmox-backup-qemu.h"
#include <nbdkit-plugin.h>

const char *timestamp = NULL;
const char *image = NULL;
const char *vmid = NULL;
const char *repo = NULL;
const char *password = NULL;
const char *fingerprint = NULL;
const char *namespace = NULL;
uint64_t backup_time;

ProxmoxRestoreHandle *pbs;

static void pbsnbd_unload(void) { free(pbs); }

static int pbsnbd_config(const char *key, const char *value) {
  if (strcmp(key, "image") == 0) {
    image = value;
    if (!image)
      return -1;
  } else if (strcmp(key, "timestamp") == 0) {
    timestamp = value;
    if (!timestamp)
      return -1;
  } else if (strcmp(key, "vmid") == 0) {
    vmid = value;
    if (!vmid)
      return -1;
  } else if (strcmp(key, "repo") == 0) {
    repo = value;
    if (!repo)
      return -1;
  } else if (strcmp(key, "password") == 0) {
    password = value;
    if (!password)
      return -1;
  } else if (strcmp(key, "fingerprint") == 0) {
    fingerprint = value;
    if (!fingerprint)
      return -1;
  } else if (strcmp(key, "namespace") == 0) {
    namespace = value;
    if (!namespace)
      return -1;
  } else {
    nbdkit_error("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

time_t my_timegm(struct tm *tm) {
  time_t local = mktime(tm);
  struct tm *gmt = gmtime(&local);

  return local + (local - mktime(gmt));
}

static int pbsnbd_config_complete(void) {
  if (image == NULL) {
    nbdkit_error("you must supply the image=<IMAGE> parameter "
                 "after the plugin name on the command line");
    return -1;
  }
  if (timestamp == NULL) {
    nbdkit_error("you must supply the timestamp=<TIMESTAMP> parameter "
                 "after the plugin name on the command line");
    return -1;
  } else {
    struct tm tm_time = {0};
    if (strptime(timestamp, "%Y-%m-%dT%H:%M:%SZ", &tm_time) == NULL) {
      nbdkit_error("Unable to convert passed timestamp");
      return -1;
    }
    tm_time.tm_isdst = -1;
    backup_time = (uint64_t)my_timegm(&tm_time);
  }
  if (repo == NULL) {
    nbdkit_error("you must supply the repo=<REPO> parameter "
                 "after the plugin name on the command line");
    return -1;
  }
  if (password == NULL) {
    nbdkit_error("you must supply the password=<PASSWORD> parameter "
                 "after the plugin name on the command line");
    return -1;
  }
  if (fingerprint == NULL) {
    nbdkit_error("you must supply the fingerprint=<FINGERPRINT> parameter "
                 "after the plugin name on the command line");
    return -1;
  }
  if (vmid == NULL) {
    nbdkit_error("you must supply the vmid=<VMID> parameter "
                 "after the plugin name on the command line");
    return -1;
  }

  return 0;
}

#define pbsnbd_config_help                                                     \
  "repo=<REPO>                  (required) The PBS repository string to "      \
  "connect.\n"                                                                 \
  "password=<PASSWORD>          (required) The PBS password.\n"                \
  "fingerprint=<FINGERPRINT>    (required) The PBS ssl fingerprint.\n"         \
  "vmid=<VMID>                  (required) The Backup ID to map\n"             \
  "timestamp=<TIMESTAMP>        (required) The Backup time to map\n"           \
  "image=<IMAGE>                (required) The Backup image to map.\n"

struct pbsnbd_handle {
  int devid;
};

static void *pbsnbd_open(int readonly) {
  char *pbs_error = NULL;

  int size = snprintf(NULL, 0, "%s%s", image, ".fidx") + 1;
  char *image_name = malloc(size);
  snprintf(image_name, size, "%s%s", image, ".fidx");

  struct pbsnbd_handle *h;
  h = malloc(sizeof *h);
  if (h == NULL) {
    nbdkit_error("malloc: %m");
    free(image_name);
    return NULL;
  }

  fprintf(stderr, "Opening image [%s]\n", image_name);
  h->devid = proxmox_restore_open_image(pbs, image_name, &pbs_error);
  if (h->devid < 0) {
    nbdkit_error("proxmox_restore_open_image failed - %s\n", pbs_error);
    proxmox_backup_free_error(pbs_error);
    free(h);
    return NULL;
  }

  return h;
}

static int pbsnbd_get_ready(void) { return 0; }

static int pbsnbd_after_fork(void) {
  char *pbs_error = NULL;
  const char *snapshot =
      proxmox_backup_snapshot_string("vm", vmid, backup_time, &pbs_error);

  if (snapshot == NULL) {
    nbdkit_error("proxmox_backup_snapshot_string failed - %s\n", pbs_error);
    proxmox_backup_free_error(pbs_error);
    return -1;
  }

  pbs = proxmox_restore_new(repo, snapshot, password, NULL, NULL, fingerprint,
                            &pbs_error);

  fprintf(stderr, "Connecting PBS: [%s]\n", repo);
  if (proxmox_restore_connect(pbs, &pbs_error) < 0) {
    nbdkit_error("proxmox_restore_connect failed - %s\n", pbs_error);
    proxmox_backup_free_error(pbs_error);
    return -1;
  }

  fprintf(stderr,
          "Connected via library version: [%s] Default chunk size: [%d]\n",
          proxmox_backup_qemu_version(), PROXMOX_BACKUP_DEFAULT_CHUNK_SIZE);

  return 0;
}

static void pbsnbd_close(void *handle) {
  struct pbsnbd_handle *h = handle;
  free(h);
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

static int64_t pbsnbd_get_size(void *handle) {
  struct pbsnbd_handle *h = handle;
  char *pbs_error = NULL;
  long length = proxmox_restore_get_image_length(pbs, h->devid, &pbs_error);
  int64_t size = (int64_t)length;

  return size;
}

static int pbsnbd_pread(void *handle, void *buf, uint32_t count,
                        uint64_t offset, uint32_t flags) {
  struct pbsnbd_handle *h = handle;
  char *pbs_error = NULL;
  int done = proxmox_restore_read_image_at(pbs, h->devid, buf, offset, count,
                                           &pbs_error);
  if (done != count) {
    nbdkit_error("pread: failed: %s", pbs_error);
  }
  return 0;
}

static struct nbdkit_plugin plugin = {
    .name = "pbsnbd",
    .version = "0.2",
    .unload = pbsnbd_unload,
    .config = pbsnbd_config,
    .config_complete = pbsnbd_config_complete,
    .config_help = pbsnbd_config_help,
    .open = pbsnbd_open,
    .close = pbsnbd_close,
    .get_size = pbsnbd_get_size,
    .pread = pbsnbd_pread,
    .get_ready = pbsnbd_get_ready,
    .after_fork = pbsnbd_after_fork,
    .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN(plugin)
