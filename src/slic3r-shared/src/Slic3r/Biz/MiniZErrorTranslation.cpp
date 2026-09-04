#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/MiniZErrorTranslation.hpp"

namespace Slic3r::Biz {

std::string translate_miniz_error(mz_zip_error mz_err)
{
    switch (mz_err) {
    case MZ_ZIP_NO_ERROR:
        return "no error";
    case MZ_ZIP_UNDEFINED_ERROR:
        return _u8L("undefined error");
    case MZ_ZIP_TOO_MANY_FILES:
        return _u8L("too many files");
    case MZ_ZIP_FILE_TOO_LARGE:
        return _u8L("file too large");
    case MZ_ZIP_UNSUPPORTED_METHOD:
        return _u8L("unsupported method");
    case MZ_ZIP_UNSUPPORTED_ENCRYPTION:
        return _u8L("unsupported encryption");
    case MZ_ZIP_UNSUPPORTED_FEATURE:
        return _u8L("unsupported feature");
    case MZ_ZIP_FAILED_FINDING_CENTRAL_DIR:
        return _u8L("failed finding central directory");
    case MZ_ZIP_NOT_AN_ARCHIVE:
        return _u8L("not a ZIP archive");
    case MZ_ZIP_INVALID_HEADER_OR_CORRUPTED:
        return _u8L("invalid header or archive is corrupted");
    case MZ_ZIP_UNSUPPORTED_MULTIDISK:
        return _u8L("unsupported multidisk archive");
    case MZ_ZIP_DECOMPRESSION_FAILED:
        return _u8L("decompression failed or archive is corrupted");
    case MZ_ZIP_COMPRESSION_FAILED:
        return _u8L("compression failed");
    case MZ_ZIP_UNEXPECTED_DECOMPRESSED_SIZE:
        return _u8L("unexpected decompressed size");
    case MZ_ZIP_CRC_CHECK_FAILED:
        return _u8L("CRC-32 check failed");
    case MZ_ZIP_UNSUPPORTED_CDIR_SIZE:
        return _u8L("unsupported central directory size");
    case MZ_ZIP_ALLOC_FAILED:
        return _u8L("allocation failed");
    case MZ_ZIP_FILE_OPEN_FAILED:
        return _u8L("file open failed");
    case MZ_ZIP_FILE_CREATE_FAILED:
        return _u8L("file create failed");
    case MZ_ZIP_FILE_WRITE_FAILED:
        return _u8L("file write failed");
    case MZ_ZIP_FILE_READ_FAILED:
        return _u8L("file read failed");
    case MZ_ZIP_FILE_CLOSE_FAILED:
        return _u8L("file close failed");
    case MZ_ZIP_FILE_SEEK_FAILED:
        return _u8L("file seek failed");
    case MZ_ZIP_FILE_STAT_FAILED:
        return _u8L("file stat failed");
    case MZ_ZIP_INVALID_PARAMETER:
        return _u8L("invalid parameter");
    case MZ_ZIP_INVALID_FILENAME:
        return _u8L("invalid filename");
    case MZ_ZIP_BUF_TOO_SMALL:
        return _u8L("buffer too small");
    case MZ_ZIP_INTERNAL_ERROR:
        return _u8L("internal error");
    case MZ_ZIP_FILE_NOT_FOUND:
        return _u8L("file not found");
    case MZ_ZIP_ARCHIVE_TOO_LARGE:
        return _u8L("archive is too large");
    case MZ_ZIP_VALIDATION_FAILED:
        return _u8L("validation failed");
    case MZ_ZIP_WRITE_CALLBACK_FAILED:
        return _u8L("write calledback failed");
    default:
        break;
    }

    return "unknown error";
}

} // namespace Slic3r::Biz
