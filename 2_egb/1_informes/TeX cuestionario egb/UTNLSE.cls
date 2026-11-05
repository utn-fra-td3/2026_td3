%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% UTNLSE.cls versión 1.1
% Clase para publicaciones científicas y documentos para la Universidad Tecnológica Nacional (UTN) Facultad Regional Avellaneda (FRA) del Laboratorio de Sistemas Embebidos
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

\NeedsTeXFormat{LaTeX2e}
\ProvidesClass{UTNLSE}[2026/07/25 example class v1.1]
\LoadClass[12pt,letterpaper,oneside]{article}

\newif\if@RT
\@RTfalse
\DeclareOption{RT}{\@RTtrue}
\DeclareOption{DOC}{\@RTfalse}
\ProcessOptions\relax

\RequirePackage{geometry}
\RequirePackage[spanish]{babel}
\RequirePackage{xcolor}
\RequirePackage{graphicx}
\RequirePackage{titlesec}
\RequirePackage{setspace}
\RequirePackage{etoolbox}
\RequirePackage{array}
\RequirePackage{tabularx}
\RequirePackage{booktabs}
\RequirePackage{hyperref}
\RequirePackage{enumitem}
\RequirePackage{tabularray}
\RequirePackage{fancyhdr}
\RequirePackage{caption}
\RequirePackage{siunitx}
\RequirePackage{xparse}
\RequirePackage{libertine}
\RequirePackage[T1]{fontenc}
\RequirePackage[utf8]{inputenc}

\geometry{
    left=25mm,
    right=25mm,
    top=22mm,
    bottom=22mm,
    headheight=15pt,
    headsep=18pt,
    footskip=12mm
}
\setstretch{1.15}
\setlength{\parindent}{1.25em}
\setlength{\parskip}{0pt}

\definecolor
    {UTNBlue}
    {RGB}
    {0,92,153}

\definecolor
    {UTNGray}
    {RGB}
    {232,233,235}

\definecolor
    {First}
    {RGB}
    {0,122,139}

\setlist{
    itemsep=.25em,
    topsep=.5em
}
\setitemize[1]{label=•}

\SetTblrInner[tblr]{
  row{odd} = {bg=white},
  row{even}= {bg=UTNGray},
  row{1}   = {bg=First, fg=white, font=\bfseries}
}
\captionsetup[table]{skip=5pt, position=above, font={bf, color=UTNBlue, small}}
\captionsetup[figure]{skip=5pt, position=below, font={bf, color=UTNBlue, small}}

\hypersetup{colorlinks=true,linkcolor=UTNBlue,citecolor=UTNBlue,urlcolor=UTNBlue}

\pagestyle{fancy}
\fancyhf{}
\fancyfoot[R]{\thepage}
\renewcommand{\headrulewidth}{0pt}
\renewcommand{\footrulewidth}{0pt}
\fancypagestyle{plain}{%
    \fancyhf{}%
    \renewcommand{\headrulewidth}{0pt}%
    \renewcommand{\footrulewidth}{0pt}%
}

\setcounter{secnumdepth}{2}

\titleformat
    {\section}
    {\Large\bfseries\color{UTNBlue}}
    {\thesection.}
    {3pt}
    {}
\titlespacing{\section}{0pt}{2ex plus 0.5ex minus .2ex}{0.3ex plus .1ex}
\titleformat
    {\subsection}
    {\large\bfseries\color{UTNBlue}}
    {\thesubsection.}
    {3pt}
    {}
\titlespacing{\subsection}{0pt}{3.25ex plus 1ex minus .2ex}{1.25ex plus .2ex}
\titleformat
    {\subsubsection}
    {\normalsize\bfseries\color{UTNBlue}}
    {\thesubsubsection.}
    {3pt}
    {}
\titlespacing{\subsubsection}{0pt}{3.25ex plus 1ex minus .2ex}{1ex plus .2ex}

\if@RT
\newcommand{\@titleES}{}
\newcommand{\@subtitleES}{}
\newcommand{\@titleEN}{}
\newcommand{\@subtitleEN}{}
\newcommand{\@correspondingemail}{}

\newcommand{\titleES}[1]{\renewcommand{\@titleES}{#1}}
\newcommand{\subtitleES}[1]{\renewcommand{\@subtitleES}{#1}}
\newcommand{\titleEN}[1]{\renewcommand{\@titleEN}{#1}}
\newcommand{\subtitleEN}[1]{\renewcommand{\@subtitleEN}{#1}}
\newcommand{\correspondingemail}[1]{\renewcommand{\@correspondingemail}{#1}}

\newcommand{\@authorsline}{}
\newcommand{\@pdfauthors}{}
\newcommand{\@affiliationsblock}{}
\newcounter{authorcnt}
\setcounter{authorcnt}{0}

\NewDocumentCommand{\addauthor}{s m m m m o}{%
    \stepcounter{authorcnt}%
    \IfBooleanTF{#1}
        {\def\@thismark{*}}
        {\def\@thismark{}}%
    \edef\@thissuper{\theauthorcnt\ \@thismark}%
    \edef\@thisaffilnum{\theauthorcnt}%
    \ifnum\value{authorcnt}=1
        \eappto{\@authorsline}{%
            [#2 #3]\noexpand\textsuperscript{\@thissuper}%
        }%
    \else
        \eappto{\@authorsline}{%
            \ $\cdot$\ [#2 #3]\noexpand\textsuperscript{\@thissuper}%
        }%
    \fi
    \eappto{\@affiliationsblock}{%
        \noexpand\textsuperscript{\@thisaffilnum}\ #4\ $\cdot$\ ORCID:~\noexpand\url{#5}\par
    }%
    \def\@thisemail{}%
    \IfValueT{#6}{\def\@thisemail{, email: #6}}
    \ifnum\value{authorcnt}=1
        \eappto{\@pdfauthors}{#2 #3 (#4, ORCID: #5\@thisemail)}%
    \else
        \eappto{\@pdfauthors}{; #2 #3 (#4, ORCID: #5\@thisemail)}%
    \fi
}

\renewcommand{\maketitle}{%
    \begingroup
    \raggedright
    \thispagestyle{empty}
    {\fontsize{24}{28}\selectfont\bfseries\color{UTNBlue}\@titleES\par}
    \ifdefempty{\@subtitleES}{}{%
        {\fontsize{21}{25}\selectfont\bfseries\color{black}\@subtitleES\par}%
    }
    \vspace{0.5em}
    {\fontsize{13}{16}\selectfont\itshape\color{gray}\@titleEN\par}
    \ifdefempty{\@subtitleEN}{}{%
        {\fontsize{10}{12}\selectfont\itshape\color{gray}\@subtitleEN\par}%
    }
    \vspace{0.5em}

    \normalfont\fontsize{13}{16}\selectfont\bfseries\color{black}%
    \ifdefempty{\@authorsline}{}{\@authorsline\par}
    \vspace{0.2em}

    \begingroup
        \normalfont\fontsize{10}{12}\selectfont\color{gray}%
        \urlstyle{same}%
        \hypersetup{urlcolor=gray}%
        \ifdefempty{\@affiliationsblock}{}{\@affiliationsblock}
    \endgroup
    \hypersetup{urlcolor=UTNBlue}%

    \ifdefempty{\@correspondingemail}{}{%
        \vspace{1em}
        \normalfont\fontsize{10}{12}\selectfont
        \textit{Autor de correspondencia}: \@correspondingemail\par
    }
    \vspace{0.2em}

    \begingroup
        \normalfont
        \normalsize
        \@printresumenes
    \endgroup

    \endgroup
    \hypersetup{%
        pdftitle={\@titleES\ifdefempty{\@subtitleES}{}{: \@subtitleES}},
        pdfauthor={\@pdfauthors},
        pdfsubject={\@titleEN\ifdefempty{\@subtitleEN}{}{: \@subtitleEN}},
        pdfkeywords={\@keywordsES\ifdefempty{\@keywordsEN}{}{; \@keywordsEN}}
    }
}

\newcommand{\@resumenES}{}
\newcommand{\@resumenEN}{}
\newcommand{\resumenES}[1]{\renewcommand{\@resumenES}{#1}}
\newcommand{\resumenEN}[1]{\renewcommand{\@resumenEN}{#1}}

\newcommand{\@keywordsES}{}
\newcommand{\@keywordsEN}{}
\newcommand{\keywordsES}[1]{\renewcommand{\@keywordsES}{#1}}
\newcommand{\keywordsEN}[1]{\renewcommand{\@keywordsEN}{#1}}

\newcommand{\@printresumenes}{%
    \begingroup
    \setlength{\parindent}{0pt}

    \ifdefempty{\@resumenES}{}{%
        \section*{Resumen}
        \@resumenES\par

        \ifdefempty{\@keywordsES}{}{%
            \vspace{0.3em}
            {\small\color{gray}
            \textbf{Palabras clave:}
            \@keywordsES\par}
        }

        \vspace{0.2em}
    }

    \ifdefempty{\@resumenEN}{}{%
        \section*{Abstract}
        \@resumenEN\par

        \ifdefempty{\@keywordsEN}{}{%
            \vspace{0.3em}
            {\small\color{gray}
            \textbf{Keywords:}
            \@keywordsEN\par}
        }
    }

    \endgroup
}

\AtBeginDocument{%
    \maketitle
    \thispagestyle{empty}
    \clearpage
}

\else


\newcommand{\@authorsline}{}
\newcounter{authorcnt}

\NewDocumentCommand{\addauthor}{mm}
{
    \ifnum\value{authorcnt}=0
        \gdef\@authorsline{#1~#2}
    \else
        \gappto{\@authorsline}{\ $\cdot$\ #1~#2}
    \fi

    \stepcounter{authorcnt}
}


\newcommand{\@doctitle}{}
\newcommand{\@docsubtitle}{}
\newcommand{\@course}{}
\newcommand{\@classnumber}{}
\newcommand{\@laboratory}{}
\newcommand{\@coverimage}{}

\renewcommand{\title}[1]
{
    \gdef\@doctitle{#1}
}

\newcommand{\subtitle}[1]
{
    \gdef\@docsubtitle{#1}
}

\NewDocumentCommand{\course}{O{}m}
{
    \gdef\@course{#2}
    \gdef\@classnumber{#1}
}

\newcommand{\laboratory}[1]
{
    \gdef\@laboratory{#1}
}

\newcommand{\LSE}
{
    \laboratory{Laboratorio de Sistemas Embebidos}
}

\newcommand{\coverimage}[1]
{
    \gdef\@coverimage{#1}
}

\renewcommand{\maketitle}
{
    \thispagestyle{empty}

    \begingroup
    \raggedright

    {\fontsize{36}{40}\selectfont
    \bfseries
    \color{UTNBlue}
    \@doctitle\par}

    \ifdefempty{\@docsubtitle}{}{%
        {\fontsize{28}{32}\selectfont
        \bfseries
        \color{black}
        \@docsubtitle\par}
    }

    \vspace{2em}


    {\fontsize{18}{22}\selectfont
    \bfseries
    \@authorsline\par}

    \vspace{1em}

    \ifdefempty{\@course}{}{%
        \textbf{\fontsize{16}{18}\selectfont\@course}%
        \ifdefempty{\@classnumber}{}{%
            \quad\quad
            {\fontsize{16}{18}\selectfont Clase N.º~\@classnumber}%
        }%
        \par
        \vspace{0.6em}
    }

    \ifdefempty{\@laboratory}{}{%
        {\fontsize{16}{21}\selectfont
        \@laboratory\par}

        \vspace{0.3em}
    }

    \ifdefempty{\@coverimage}{}{%
        \vfill
        \begin{center}
            \includegraphics[width=5cm]{\@coverimage}
        \end{center}
        \vfill
    }

    {\fontsize{18}{21}\selectfont
    Universidad Tecnológica Nacional\\
    Facultad Regional Avellaneda\\
    Departamento de Ingeniería Electrónica

    \par}

    \clearpage

    \endgroup
}

\AtBeginDocument{%
    \maketitle
    \tableofcontents
    \thispagestyle{empty}
    \clearpage
}

\fancyhead[L]{\footnotesize\@docsubtitle}
\fancyhead[R]{\footnotesize\nouppercase{\leftmark}}
\fancyfoot[C]{}
\fancyfoot[L]{%
    \footnotesize
    \@authorsline%
    \ifdefempty{\@laboratory}{}{%
        \ --\ \@laboratory%
    }%
    \ --\ \@doctitle%
}
\fancyfoot[R]{\footnotesize\thepage}

\renewcommand{\headrulewidth}{0pt}
\renewcommand{\footrulewidth}{0pt}
\fi