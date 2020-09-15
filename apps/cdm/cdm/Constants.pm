## Module stuff
package Constants;
use strict;

use Exporter qw(import);

####
use File::Basename;
use Cwd qw(abs_path);
use constant PATH_CDM_SOURCE_DIRECTORY => dirname (abs_path(__FILE__));
####

#use lib "/home/t2/MFM/res/perllib";
use MFZUtils qw(:constants);   # Pull in packing formats from MFZUtils (and reexport below)

use constant CDM_PROTOCOL_VERSION_SRSLYNOW => 3;    # 202008260233 OO Refactor & cleanup
use constant CDM_PROTOCOL_VERSION_PIPELINE => 2;    # 202008140223 Pipeline overlay
use constant CDM_PROTOCOL_VERSION_ASPINNER => 1;    # Pre-version-protocol version
use constant CDM_PROTOCOL_VERSION_PREHISTORY => 0;  
use constant CDM_PROTOCOL_VERSION_UNKNOWN => -1;  

###########
use constant CDM_PROTOCOL_OUR_VERSION => CDM_PROTOCOL_VERSION_SRSLYNOW;
###########

use constant DIR8_SERVER => 8; # Special code for us

use constant NGB_STATE_INIT => 0;
use constant NGB_STATE_CLSD => NGB_STATE_INIT+1;  # /sys/class/itc_pkt/status[dir8] == 0
use constant NGB_STATE_OPEN => NGB_STATE_CLSD+1;  # /sys/class/itc_pkt/status[dir8] > 0
use constant NGB_STATE_LIVE => NGB_STATE_OPEN+1;  # Some CDM packet received recently

use constant MFZ_STATE_INIT => 0;
use constant MFZ_STATE_NOGO => MFZ_STATE_INIT+1;  # File is known invalid
use constant MFZ_STATE_FILE => MFZ_STATE_NOGO+1;  # Has a (perhaps) stub file in filePath
use constant MFZ_STATE_CPLF => MFZ_STATE_FILE+1;  # Content configured by PF packet
use constant MFZ_STATE_CCNV => MFZ_STATE_CPLF+1;  # Content is complete and verified
use constant MFZ_STATE_DEAD => MFZ_STATE_CCNV+1;  # MFZManager is dead, do not use

use constant MAX_CONTENT_NAME_LENGTH => 28;
use constant MAX_MFZ_NAME_LENGTH => MAX_CONTENT_NAME_LENGTH+4; # 4 for '.mfz'

use constant MAX_D_TYPE_DATA_LENGTH => 180;

use constant SUBDIR_COMMON => "common";
use constant SUBDIR_LOG => "log";
use constant SUBDIR_PENDING => "pending";
use constant SUBDIR_PIPELINE => "pipeline";
use constant SUBDIR_PUBKEY => "public_keys";

use constant PATH_PROG_MFZRUN => "${\PATH_CDM_SOURCE_DIRECTORY}/mfzrun";
use constant PATH_DATA_IOSTATS => "/sys/class/itc_pkt/statistics";
use constant PATH_BASEDIR_REPORT_IOSTATS => "log/status.txt";

use constant CDM_DELETEDS_MFZ => "cdm-deleteds.mfz";
use constant CDMD_T2_12_MFZ =>   "cdmd-T2-12.mfz";
use constant CDMD_MFM_MFZ =>     "cdmd-MFM.mfz";

use constant HOOK_TYPE_LOAD => "LOAD";
use constant HOOK_TYPE_RELEASE => "RELEASE";

my @subdirs = qw(
    SUBDIR_COMMON
    SUBDIR_LOG
    SUBDIR_PENDING
    SUBDIR_PIPELINE
    SUBDIR_PUBKEY
    );

my @mfzfiles = qw(
    CDM_DELETEDS_MFZ
    CDMD_T2_12_MFZ
    CDMD_MFM_MFZ
    );

my @constants = qw(
    CDM_PROTOCOL_VERSION_SRSLYNOW
    CDM_PROTOCOL_VERSION_PIPELINE 
    CDM_PROTOCOL_VERSION_ASPINNER
    CDM_PROTOCOL_VERSION_PREHISTORY
    CDM_PROTOCOL_VERSION_UNKNOWN
    CDM_PROTOCOL_OUR_VERSION

    DIR8_SERVER

    CDM_FORMAT_MAGIC
    CDM_FORMAT_VERSION_MAJOR
    CDM_FORMAT_VERSION_MINOR

    CDM10_PACK_SIGNED_DATA_FORMAT
    CDM10_PACK_FULL_FILE_FORMAT

    NGB_STATE_INIT
    NGB_STATE_CLSD
    NGB_STATE_OPEN
    NGB_STATE_LIVE

    MFZ_STATE_INIT
    MFZ_STATE_NOGO
    MFZ_STATE_FILE
    MFZ_STATE_CPLF
    MFZ_STATE_CCNV
    MFZ_STATE_DEAD

    MAX_CONTENT_NAME_LENGTH
    MAX_MFZ_NAME_LENGTH
    MAX_D_TYPE_DATA_LENGTH

    HOOK_TYPE_LOAD
    HOOK_TYPE_RELEASE

    SS_SLOT_BITS
    SS_SLOT_MASK
    SS_STAMP_BITS
    SS_STAMP_MASK

    );

my @paths = qw(
    PATH_CDM_SOURCE_DIRECTORY
    PATH_PROG_MFZRUN
    PATH_DATA_IOSTATS
    PATH_BASEDIR_REPORT_IOSTATS
    );

our @EXPORT_OK = (@constants, @subdirs, @mfzfiles, @paths);
our %EXPORT_TAGS =
    (
     constants => \@constants,
     subdirs => \@subdirs,
     mfzfiles => \@mfzfiles,
     paths => \@paths,
     all => \@EXPORT_OK
    );

1;
