
#ifndef HTTP_STATUS_CODE_H
#define HTTP_STATUS_CODE_H 1

struct http_status_code_s {
  int code;
  char* scode;
  char* text;
};

extern struct http_status_code_s http_status_codes[];

enum HTTPSTATUSCODES {
  HTTPSC_Continue = 0,
  HTTPSC_SwitchingProtocols,
  HTTPSC_OK,
  HTTPSC_Created,
  HTTPSC_Accepted,
  HTTPSC_NonAuthoritativeInformation,
  HTTPSC_NoContent,
  HTTPSC_ResetContent,
  HTTPSC_PartialContent,
  HTTPSC_MultipleChoices,
  HTTPSC_MovedPermanently,
  HTTPSC_Found,
  HTTPSC_SeeOther,
  HTTPSC_NotModified,
  HTTPSC_UseProxy,
  HTTPSC_TemporaryRedirect,
  HTTPSC_BadRequest,
  HTTPSC_Unauthorized,
  HTTPSC_PaymentRequired,
  HTTPSC_Forbidden,
  HTTPSC_NotFound,
  HTTPSC_MethodNotAllowed,
  HTTPSC_NotAcceptable,
  HTTPSC_ProxyAuthenticationRequired,
  HTTPSC_RequestTimeout,
  HTTPSC_Conflict,
  HTTPSC_Gone,
  HTTPSC_LengthRequired,
  HTTPSC_PreconditionFailed,
  HTTPSC_RequestEntityTooLarge,
  HTTPSC_RequestURITooLarge,
  HTTPSC_UnsupportedMediaType,
  HTTPSC_Requestedrangenotsatisfiable,
  HTTPSC_ExpectationFailed,
  HTTPSC_RequestHeaderFieldsTooLarge,
  HTTPSC_InternalServerError,
  HTTPSC_NotImplemented,
  HTTPSC_BadGateway,
  HTTPSC_ServiceUnavailable,
  HTTPSC_GatewayTimeout,
  HTTPSC_HTTPVersionnotsupported,
  HTTPSC_LAST_VALUE
};
#endif
