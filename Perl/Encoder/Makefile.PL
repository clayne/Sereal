use 5.008;

use strict;
use warnings;

use ExtUtils::MakeMaker;
use Config;

our $VERSION= '5.004_001'; # what version are we and what version of Decoder do we need.
$VERSION = eval $VERSION or die "WTF: $VERSION: $@"; # deal with underbars

my $shared_dir= "../shared";
my $its_our_repo_file= "../this_is_the_Sereal_repo.txt";
my $in_source_repo= ( -d "../../.git" &&
                      -d $shared_dir &&
                      -e $its_our_repo_file );
my $do_prompt= !$in_source_repo || $ENV{CPAN_COMPAT};

unshift @INC, ".", "./inc";
unshift @INC, $shared_dir, "$shared_dir/inc"
    if $in_source_repo;

my $module= "Sereal::Encoder";
require inc::Sereal::BuildTools;
if ($in_source_repo) {
    inc::Sereal::BuildTools::link_files($shared_dir);
    inc::Sereal::BuildTools::generate_constant_includes($module);
}

# Note you can Configure/optimize for miniz by modifying miniz.h
#   * Important: For best perf. be sure to customize the below macros for your target platform:
#     #define MINIZ_USE_UNALIGNED_LOADS_AND_STORES 1
#     #define MINIZ_LITTLE_ENDIAN 1
#     #define MINIZ_HAS_64BIT_REGISTERS 1
# Or you can set USE_UNALIGNED in the env before running Makefile.PL

if ($do_prompt and !defined $ENV{USE_UNALIGNED}) {
    my $use_unaligned= prompt("USE_UNALIGNED not set in env. Use unaligned loads and stores? yes/no","no");
    $ENV{USE_UNALIGNED} = $use_unaligned eq "yes";
} else {
    $ENV{USE_UNALIGNED} ||= 1;
    $ENV{USE_UNALIGNED} = 0 if $ENV{USE_UNALIGNED} ne 1
                            && $ENV{USE_UNALIGNED} ne "yes";
}

if ($do_prompt and !defined $ENV{NO_ASM} and !defined $ENV{ZSTD_DISABLE_ASM}) {
    my $use_asm= prompt("neither NO_ASM nor ZSTD_DISABLE_ASM set in env. Use assembly in ZSTD? yes/no","yes");
    $ENV{NO_ASM} = $use_asm ne "yes";
}

my $optimize= inc::Sereal::BuildTools::build_optimize();

my $libs= '';
my $subdirs= [];
my $objects= '$(BASEEXT)$(OBJ_EXT) srl_encoder$(OBJ_EXT)';
my $defines= inc::Sereal::BuildTools::build_defines('ENABLE_DANGEROUS_HACKS');
my $inc= '-I.';
$inc .= ' -I/usr/local/include'
    if $^O =~ /openbsd/;

# Prefer external libraries over the bundled one.
inc::Sereal::BuildTools::check_external_libraries( \$libs, \$defines, \$objects, $subdirs );

if ( $defines !~ /HAVE_CSNAPPY/ ) {
    # from Compress::Snappy
    require Devel::CheckLib;
    my $ctz= Devel::CheckLib::check_lib(
        lib      => 'c',
        function => 'return (__builtin_ctzll(0x100000000LL) != 32);'
    ) ? '-DHAVE_BUILTIN_CTZ' : '';
    $defines .= " $ctz" if $ctz;
}

# See lib/ExtUtils/MakeMaker.pm for details of how to influence
# the contents of the Makefile that is written.
inc::Sereal::BuildTools::WriteMakefile(
    MIN_PERL_VERSION => '5.008',
    META_MERGE       => {
        resources => {
            repository => {
                url => 'git://github.com/Sereal/Sereal.git',
            },
            bugtracker => {
                web => 'https://github.com/Sereal/Sereal/issues',
            },
        },
        'meta-spec' => { version => 2 },
    },
    TEST_REQUIRES => {
        'Data::Dumper'      => '0',
        'File::Spec'        => '0',
        'Hash::Util'        => '0',
        'Scalar::Util'      => '0',
        'Sereal::Decoder'   => $VERSION,
        'Test::Deep'        => '0',
        'Test::Differences' => '0',
        'Test::LongString'  => '0',
        'Test::More'        => '0.88',
        'Test::Warn'        => '0',
    },
    BUILD_REQUIRES => {
        'ExtUtils::ParseXS' => '2.21',
    },
    CONFIGURE_REQUIRES => {
        'Devel::CheckLib'       => '1.16',
        'ExtUtils::MakeMaker'   => '7.0',
    },
    NAME         => $module,
    VERSION_FROM => 'lib/Sereal/Encoder.pm',    # finds $VERSION
    PREREQ_PM    => {
        'Sereal::Decoder' => $VERSION,
        'XSLoader' => 0,
    },                                          # e.g., Module::Name => 1.1
    LICENSE       => 'perl',
    ABSTRACT_FROM => 'lib/Sereal/Encoder.pm',
    AUTHOR        => 'Steffen Mueller <smueller@cpan.org>, Yves Orton <yves@cpan.org>',
    LIBS          => [$libs],   # e.g., '-lm'
    DEFINE        => $defines,
    INC      => $inc,           # e.g., '-I. -I/usr/local/include'
    OPTIMIZE => $optimize,
    DIR      => $subdirs,
    OBJECT   => $objects,
    test     => {
        TESTS => "t/*.t t/*/*/*.t",
    },
);
$ENV{OPTIMIZE}= $optimize;
