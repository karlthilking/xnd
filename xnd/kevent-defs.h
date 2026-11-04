/* kevent-defs.h */
#ifndef XND_KEVENT_DEFS_H
#define XND_KEVENT_DEFS_H

/* Additional filter types in event.h */
#define EVFILT_UNUSED_11        (-11)   /* (-11) unused */
#define EVFILT_SOCK             (-13)   /* Socket events */
#define EVFILT_MEMORYSTATUS     (-14)   /* Memorystatus events */
#define EVFILT_NW_CHANNEL       (-16)   /* Skywalk channel events */
#define EVFILT_WORKLOOP         (-17)   /* Workloop events */
#define EVFILT_EXCLAVES_NOTIFICATION (-18) /* Exclave notification */

struct kevent_qos_s {
	uint64_t        ident;          /* identifier for this event */
	int16_t         filter;         /* filter for event */
	uint16_t        flags;          /* general flags */
	int32_t         qos;            /* quality of service */
	uint64_t        udata;          /* opaque user data identifier */
	uint32_t        fflags;         /* filter-specific flags */
	uint32_t        xflags;         /* extra filter-specific flags */
	int64_t         data;           /* filter-specific data */
	uint64_t        ext[4];         /* filter-specific extensions */
};

typedef uint64_t kqueue_id_t;

#define KEVENT_FLAG_STACK_DATA                   0x000008
#define KEVENT_FLAG_WORKQ                        0x000020
#define KEVENT_FLAG_WORKLOOP                     0x000400
#define KEVENT_FLAG_PARKING                      0x000800
#define KEVENT_FLAG_DYNAMIC_KQ_MUST_EXIST        0x020000
#define KEVENT_FLAG_DYNAMIC_KQ_MUST_NOT_EXIST    0x040000

extern int kevent_qos(int kq,
                      const struct kevent_qos_s *changelist,
                      int nchanges,
                      struct kevent_qos_s *eventlist,
                      int nevents,
                      void *data_out,
                      size_t *data_available,
                      unsigned int flags);

extern int kevent_id(kqueue_id_t id,
                     const struct kevent_qos_s *changelist,
                     int nchanges,
                     struct kevent_qos_s *eventlist,
                     int nevents,
                     void *data_out,
                     size_t *data_available,
                     unsigned int flags);

#endif /* XND_KEVENT_DEFS_H */
