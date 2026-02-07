#!/usr/bin/env Rscript

suppressWarnings(suppressMessages({
    require(httr, quietly = TRUE, warn.conflicts = FALSE)
    require(jsonlite, quietly = TRUE, warn.conflicts = FALSE)
}))

OECD_MEMBERS_ISO3 <- c(
    "AUS","AUT","BEL","CAN","CHL","COL","CRI","CZE","DNK","EST",
    "FIN","FRA","DEU","GRC","HUN","ISL","IRL","ISR","ITA","JPN",
    "KOR","LVA","LTU","LUX","MEX","NLD","NZL","NOR","POL","PRT",
    "SVK","SVN","ESP","SWE","CHE","TUR","GBR","USA"
)

.oecd_sdmx_base <- "https://sdmx.oecd.org/public/rest"
.ua <- sprintf("oecd_steel_panel/1.0 (R %s)", getRversion())
.dbn_base <- "https://api.db.nomics.world/v22"

.cache_env <- new.env(parent = emptyenv())
.cache_env$structure <- new.env(parent = emptyenv())

stop_if_missing_pkgs <- function() {
    if (!requireNamespace("httr", quietly = TRUE)) {
        stop("Missing R package 'httr'. Install it and retry.", call. = FALSE)
    }
    if (!requireNamespace("jsonlite", quietly = TRUE)) {
        stop("Missing R package 'jsonlite'. Install it and retry.", call. = FALSE)
    }
}

dbn_get_json <- function(path, query = list(), max_tries = 6, timeout_sec = 90) {
    url <- paste0(.dbn_base, path)
    headers <- c(`User-Agent` = .ua)

    resp <- httr::RETRY(
        verb = "GET",
        url = url,
        query = query,
        times = max_tries,
        pause_base = 0.8,
        pause_cap = 6,
        terminate_on = c(400, 401, 403, 404, 422),
        httr::add_headers(.headers = headers),
        httr::timeout(timeout_sec)
    )

    status <- httr::status_code(resp)
    txt <- httr::content(resp, as = "text", encoding = "UTF-8")

    if (status == 404) {
        return(NULL)
    }
    if (status >= 400) {
        stop(sprintf("HTTP %d from %s\n%s", status, url, substr(txt, 1, 400)), call. = FALSE)
    }

    jsonlite::fromJSON(txt, simplifyVector = FALSE)
}

dbn_docs_to_long <- function(docs, value_name, keep_dims = c("REF_AREA")) {
    if (is.null(docs) || length(docs) == 0) {
        return(empty_series_df(value_name))
    }

    out <- list()
    out_i <- 0L

    for (doc in docs) {
        dims <- doc$dimensions
        period <- doc$period
        value <- doc$value
        if (is.null(period) || is.null(value) || length(period) == 0) {
            next
        }

        df <- data.frame(
            TIME_PERIOD = unlist(period, use.names = FALSE),
            stringsAsFactors = FALSE
        )
        df[[value_name]] <- suppressWarnings(as.numeric(unlist(value, use.names = FALSE)))

        if (!is.null(dims) && length(dims) > 0) {
            for (k in keep_dims) {
                if (!is.null(dims[[k]])) {
                    df[[k]] <- as.character(dims[[k]])
                }
            }
        }

        out_i <- out_i + 1L
        out[[out_i]] <- df
    }

    if (length(out) == 0) {
        return(empty_series_df(value_name))
    }

    do.call(rbind, out)
}

dbn_fetch_series_ids <- function(series_ids, value_name, chunk_size = 25L) {
    if (length(series_ids) == 0) {
        return(empty_series_df(value_name))
    }

    chunks <- split(series_ids, ceiling(seq_along(series_ids) / chunk_size))
    out <- list()
    out_i <- 0L

    for (ch in chunks) {
        Sys.sleep(0.1)
        q <- list(
            series_ids = paste(ch, collapse = ","),
            format = "json",
            metadata = "false",
            observations = "true"
        )
        j <- dbn_get_json("/series", query = q)
        docs <- j$series$docs
        out_i <- out_i + 1L
        out[[out_i]] <- dbn_docs_to_long(docs, value_name = value_name, keep_dims = c("REF_AREA"))
    }

    do.call(rbind, out)
}

oecd_series_id <- function(dataset_code, series_code) {
    paste("OECD", dataset_code, series_code, sep = "/")
}

oecd_series_ids_for_countries <- function(dataset_code, series_code_fn, countries) {
    vapply(countries, function(cc) oecd_series_id(dataset_code, series_code_fn(cc)), character(1))
}

http_get_text <- function(url, query = list(), accept = NULL, max_tries = 6, timeout_sec = 90) {
    headers <- c(`User-Agent` = .ua)
    if (!is.null(accept)) {
        headers <- c(headers, Accept = accept)
    }

    resp <- httr::RETRY(
        verb = "GET",
        url = url,
        query = query,
        times = max_tries,
        pause_base = 0.8,
        pause_cap = 6,
        terminate_on = c(400, 401, 403, 404, 422),
        httr::add_headers(.headers = headers),
        httr::timeout(timeout_sec)
    )

    status <- httr::status_code(resp)
    txt <- httr::content(resp, as = "text", encoding = "UTF-8")

    if (status == 429) {
        stop(
            paste0(
                "HTTP 429 (rate limit) from ", url, "\n",
                "The OECD API limited this download. Try reducing countries, setting a narrower time range, ",
                "or waiting and retrying.\n",
                substr(txt, 1, 400)
            ),
            call. = FALSE
        )
    }

    if (status == 404) {
        return(structure(list(text = txt, status = status), class = "http_404"))
    }
    if (status >= 400) {
        stop(sprintf("HTTP %d from %s\n%s", status, url, substr(txt, 1, 400)), call. = FALSE)
    }

    txt
}

sdmx_get_structure <- function(flow_id) {
    if (exists(flow_id, envir = .cache_env$structure, inherits = FALSE)) {
        return(get(flow_id, envir = .cache_env$structure, inherits = FALSE))
    }

    url <- sprintf("%s/data/%s", .oecd_sdmx_base, flow_id)
    Sys.sleep(0.25)
    txt <- http_get_text(
        url,
        query = list(firstNObservations = 1),
        accept = "application/vnd.sdmx.data+json;version=1.0"
    )
    j <- jsonlite::fromJSON(txt, simplifyVector = FALSE)
    data <- j$data

    series_dims <- vapply(
        data$structure$dimensions$series,
        function(d) d$id,
        character(1)
    )

    out <- list(
        series_dims = series_dims,
        observation_dim = data$structure$dimensions$observation[[1]]$id
    )

    assign(flow_id, out, envir = .cache_env$structure)
    out
}

sdmx_build_key <- function(flow_id, filters) {
    st <- sdmx_get_structure(flow_id)
    dims <- st$series_dims
    parts <- character(length(dims))

    for (i in seq_along(dims)) {
        dim_id <- dims[[i]]
        if (!is.null(filters[[dim_id]])) {
            val <- filters[[dim_id]]
            if (length(val) > 1) {
                parts[[i]] <- paste(val, collapse = "+")
            } else {
                parts[[i]] <- as.character(val)
            }
        } else {
            parts[[i]] <- ""
        }
    }

    paste(parts, collapse = ".")
}

sdmx_fetch <- function(flow_id, filters, query = list()) {
    key <- sdmx_build_key(flow_id, filters)
    url <- sprintf("%s/data/%s/%s", .oecd_sdmx_base, flow_id, key)
    Sys.sleep(0.25)
    txt <- http_get_text(
        url,
        query = query,
        accept = "application/vnd.sdmx.data+json;version=1.0"
    )

    if (inherits(txt, "http_404")) {
        return(NULL)
    }
    if (identical(trimws(txt), "NoRecordsFound")) {
        return(NULL)
    }

    j <- jsonlite::fromJSON(txt, simplifyVector = FALSE)
    j$data
}

sdmx_to_long <- function(data, value_name = "value") {
    dims <- data$structure$dimensions$series
    dim_ids <- vapply(dims, function(d) d$id, character(1))
    dim_values <- lapply(dims, function(d) vapply(d$values, function(v) v$id, character(1)))

    obs_dim <- data$structure$dimensions$observation[[1]]
    time_ids <- vapply(obs_dim$values, function(v) v$id, character(1))

    series <- data$dataSets[[1]]$series
    if (is.null(series) || length(series) == 0) {
        return(data.frame())
    }

    out_rows <- list()
    out_i <- 0L

    for (sname in names(series)) {
        idx <- as.integer(strsplit(sname, ":", fixed = TRUE)[[1]])
        dim_map <- setNames(
            vapply(seq_along(dim_ids), function(k) dim_values[[k]][idx[[k]] + 1L], character(1)),
            dim_ids
        )

        obs <- series[[sname]]$observations
        if (is.null(obs) || length(obs) == 0) {
            next
        }

        for (t_idx in names(obs)) {
            v <- obs[[t_idx]][[1]]
            if (length(v) == 0) {
                next
            }
            out_i <- out_i + 1L
            out_rows[[out_i]] <- c(dim_map, TIME_PERIOD = time_ids[as.integer(t_idx) + 1L], v)
        }
    }

    if (length(out_rows) == 0) {
        return(data.frame())
    }

    mat <- do.call(rbind, out_rows)
    df <- as.data.frame(mat, stringsAsFactors = FALSE)
    names(df)[ncol(df)] <- value_name
    df[[value_name]] <- suppressWarnings(as.numeric(df[[value_name]]))
    df
}

empty_series_df <- function(value_name) {
    out <- data.frame(
        REF_AREA = character(0),
        TIME_PERIOD = character(0),
        stringsAsFactors = FALSE
    )
    out[[value_name]] <- numeric(0)
    out
}

parse_quarter_to_month_end <- function(qstr) {
    # "YYYY-Qn" -> "YYYY-MM" (end month of quarter)
    parts <- strsplit(qstr, "-Q", fixed = TRUE)[[1]]
    yr <- as.integer(parts[[1]])
    qn <- as.integer(parts[[2]])
    mm <- c(3, 6, 9, 12)[qn]
    sprintf("%04d-%02d", yr, mm)
}

interpolate_quarterly_to_monthly <- function(df_q, country_col = "REF_AREA", time_col = "TIME_PERIOD", value_col = "value") {
    if (nrow(df_q) == 0) {
        return(data.frame())
    }

    df_q <- df_q[!is.na(df_q[[value_col]]), , drop = FALSE]
    if (nrow(df_q) == 0) {
        return(data.frame())
    }

    countries <- unique(df_q[[country_col]])
    out <- list()
    out_i <- 0L

    for (cc in countries) {
        d <- df_q[df_q[[country_col]] == cc, , drop = FALSE]
        # TIME_PERIOD is like "2025-Q3"
        month_end <- vapply(d[[time_col]], parse_quarter_to_month_end, character(1))

        # Convert "YYYY-MM" to a numeric month index for interpolation.
        y <- as.integer(substr(month_end, 1, 4))
        m <- as.integer(substr(month_end, 6, 7))
        # Month index: Jan 0, Feb 1, ..., Dec 11.
        t_q <- y * 12L + (m - 1L)
        o <- order(t_q)
        t_q <- t_q[o]
        v_q <- d[[value_col]][o]

        t_m <- seq.int(min(t_q), max(t_q), by = 1L)
        v_m <- stats::approx(x = t_q, y = v_q, xout = t_m, method = "linear", ties = "ordered")$y

        y_m <- t_m %/% 12L
        m_m <- (t_m %% 12L) + 1L
        month <- sprintf("%04d-%02d", y_m, m_m)

        out_i <- out_i + 1L
        out[[out_i]] <- data.frame(
            country = cc,
            month = month,
            gdp_ix_lr = v_m,
            stringsAsFactors = FALSE
        )
    }

    do.call(rbind, out)
}

safe_mkdir <- function(path) {
    if (!dir.exists(path)) {
        dir.create(path, recursive = TRUE, showWarnings = FALSE)
    }
}

as_gretl_path <- function(path) {
    # Gretl accepts POSIX-style absolute paths on Linux; normalize for safety.
    normalizePath(path, winslash = "/", mustWork = FALSE)
}

write_gretl_import_script <- function(path, csv_path, gdt_path, unit_var, time_var, panel_pd, panel_start) {
    csv_path <- as_gretl_path(csv_path)
    gdt_path <- as_gretl_path(gdt_path)

    lines <- c(
        sprintf("open \"%s\" --quiet", csv_path),
        sprintf("setobs %s %s --panel-vars", unit_var, time_var),
        sprintf("setobs %s %s --panel-time", panel_pd, panel_start),
        sprintf("store \"%s\"", gdt_path)
    )
    writeLines(lines, con = path, sep = "\n")
}

build_oecd_steel_panel <- function(
    out_dir = "oecd_steel_out",
    countries = OECD_MEMBERS_ISO3,
    make_gdt = TRUE,
    source = c("dbnomics", "oecd_sdmx")
) {
    stop_if_missing_pkgs()
    safe_mkdir(out_dir)
    source <- match.arg(source)

    if (source != "dbnomics") {
        stop("Only source='dbnomics' is supported in this build (OECD API is rate-limited).", call. = FALSE)
    }

    # ---- Annual GDP ----
    ds_table1 <- "DSD_NAMAIN10@DF_TABLE1"
    gdp_real_ids <- oecd_series_ids_for_countries(ds_table1, function(cc) {
        sprintf("A.%s.S1.S1.B1GQ._Z._Z._Z.USD_PPP.LR.N.T0102", cc)
    }, countries)
    gdp_nom_ids <- oecd_series_ids_for_countries(ds_table1, function(cc) {
        sprintf("A.%s.S1.S1.B1GQ._Z._Z._Z.XDC.V.N.T0102", cc)
    }, countries)
    gdp_g1_ids <- oecd_series_ids_for_countries(ds_table1, function(cc) {
        sprintf("A.%s.S1.S1.B1GQ._Z._Z._Z.PC.L.G1.T0102", cc)
    }, countries)

    gdp_real_df <- dbn_fetch_series_ids(gdp_real_ids, "gdp_real_usdppp_lr")
    gdp_nom_df <- dbn_fetch_series_ids(gdp_nom_ids, "gdp_nom_xdc_v")
    gdp_g1_df <- dbn_fetch_series_ids(gdp_g1_ids, "gdp_growth_pc_g1")

    # ---- Annual steel capacity ----
    ds_cap <- "DSD_STEEL_CAPACITY@DF_CRUDE_STEEL"
    cap_ids <- oecd_series_ids_for_countries(ds_cap, function(cc) {
        sprintf("%s.A.CRUDE_STEEL_CAP.T._T", cc)
    }, countries)
    cap_df <- dbn_fetch_series_ids(cap_ids, "steel_capacity_tonnes")

    # ---- Annual basic metals (C24) and fabricated metals (C25) ----
    ds_table6 <- "DSD_NAMAIN10@DF_TABLE6"
    c24_nom_ids <- oecd_series_ids_for_countries(ds_table6, function(cc) {
        sprintf("A.%s.S1.S1.B1G._Z.C24._Z.XDC.V.N.T0301", cc)
    }, countries)
    c25_nom_ids <- oecd_series_ids_for_countries(ds_table6, function(cc) {
        sprintf("A.%s.S1.S1.B1G._Z.C25._Z.XDC.V.N.T0301", cc)
    }, countries)
    c24_real_lr_ids <- oecd_series_ids_for_countries(ds_table6, function(cc) {
        sprintf("A.%s.S1.S1.B1G._Z.C24._Z.XDC.LR.N.T0301", cc)
    }, countries)
    c25_real_lr_ids <- oecd_series_ids_for_countries(ds_table6, function(cc) {
        sprintf("A.%s.S1.S1.B1G._Z.C25._Z.XDC.LR.N.T0301", cc)
    }, countries)
    c24_defl_ids <- oecd_series_ids_for_countries(ds_table6, function(cc) {
        sprintf("A.%s.S1.S1.B1G._Z.C24._Z.IX.DR.N.T0301", cc)
    }, countries)
    c25_defl_ids <- oecd_series_ids_for_countries(ds_table6, function(cc) {
        sprintf("A.%s.S1.S1.B1G._Z.C25._Z.IX.DR.N.T0301", cc)
    }, countries)

    c24_nom <- dbn_fetch_series_ids(c24_nom_ids, "c24_gva_nom_xdc_v")
    c25_nom <- dbn_fetch_series_ids(c25_nom_ids, "c25_gva_nom_xdc_v")
    c24_real_lr <- dbn_fetch_series_ids(c24_real_lr_ids, "c24_gva_real_xdc_lr")
    c25_real_lr <- dbn_fetch_series_ids(c25_real_lr_ids, "c25_gva_real_xdc_lr")
    c24_defl <- dbn_fetch_series_ids(c24_defl_ids, "c24_gva_deflator_ix_dr")
    c25_defl <- dbn_fetch_series_ids(c25_defl_ids, "c25_gva_deflator_ix_dr")

    # ---- Assemble annual panel ----
    merge_keys <- c("REF_AREA", "TIME_PERIOD")
    annual <- Reduce(
        function(x, y) merge(x, y, by = merge_keys, all = TRUE),
        list(gdp_real_df, gdp_nom_df, gdp_g1_df, cap_df, c24_nom, c25_nom, c24_real_lr, c25_real_lr, c24_defl, c25_defl)
    )

    names(annual)[names(annual) == "REF_AREA"] <- "country"
    names(annual)[names(annual) == "TIME_PERIOD"] <- "year"
    annual$year <- suppressWarnings(as.integer(annual$year))

    annual$c24_gva_share <- with(annual, c24_gva_nom_xdc_v / gdp_nom_xdc_v)
    annual$c25_gva_share <- with(annual, c25_gva_nom_xdc_v / gdp_nom_xdc_v)

    annual <- annual[order(annual$country, annual$year), , drop = FALSE]

    annual_csv <- file.path(out_dir, "oecd_steel_panel_annual.csv")
    utils::write.csv(annual, annual_csv, row.names = FALSE, na = "")
    annual_gdt <- file.path(out_dir, "oecd_steel_panel_annual.gdt")

    # ---- Monthly panel (GDP interpolated from quarterly QNA) ----
    ds_qna <- "DSD_NAMAIN1@DF_QNA"
    gdp_q_ids <- oecd_series_ids_for_countries(ds_qna, function(cc) {
        sprintf("Q.Y.%s.S1.S1.B1GQ._Z._Z._Z.IX.LR.N.T0102", cc)
    }, countries)
    gdp_q_df <- dbn_fetch_series_ids(gdp_q_ids, "gdp_ix_lr_q")
    monthly <- interpolate_quarterly_to_monthly(gdp_q_df, country_col = "REF_AREA", time_col = "TIME_PERIOD", value_col = "gdp_ix_lr_q")

    # Attach annual steel-sector shares and capacity (forward-filled by year).
    if (nrow(monthly) > 0 && nrow(annual) > 0) {
        monthly$year <- as.integer(substr(monthly$month, 1, 4))
        ann_small <- annual[, c("country", "year", "steel_capacity_tonnes", "c24_gva_share", "c25_gva_share"), drop = FALSE]
        monthly <- merge(monthly, ann_small, by = c("country", "year"), all.x = TRUE, all.y = FALSE)
        monthly$year <- NULL
        monthly <- monthly[order(monthly$country, monthly$month), , drop = FALSE]
    }

    monthly_csv <- file.path(out_dir, "oecd_steel_panel_monthly.csv")
    utils::write.csv(monthly, monthly_csv, row.names = FALSE, na = "")
    monthly_gdt <- file.path(out_dir, "oecd_steel_panel_monthly.gdt")

    # ---- Gretl import scripts ----
    annual_min_year <- min(annual$year, na.rm = TRUE)
    if (!is.finite(annual_min_year)) {
        annual_min_year <- 1
    }
    write_gretl_import_script(
        path = file.path(out_dir, "import_oecd_steel_annual.inp"),
        csv_path = annual_csv,
        gdt_path = annual_gdt,
        unit_var = "country",
        time_var = "year",
        panel_pd = 1,
        panel_start = annual_min_year
    )

    if (nrow(monthly) > 0) {
        first_month <- sort(monthly$month)[1]
        start_year <- as.integer(substr(first_month, 1, 4))
        start_m <- as.integer(substr(first_month, 6, 7))
        panel_start <- sprintf("%d:%d", start_year, start_m)
        write_gretl_import_script(
            path = file.path(out_dir, "import_oecd_steel_monthly.inp"),
            csv_path = monthly_csv,
            gdt_path = monthly_gdt,
            unit_var = "country",
            time_var = "month",
            panel_pd = 12,
            panel_start = panel_start
        )
    }

    # ---- Optionally produce .gdt via gretlcli ----
    if (isTRUE(make_gdt)) {
        gretlcli_local <- file.path(getwd(), "cli", "gretlcli")
        gretlcli <- if (file.exists(gretlcli_local)) gretlcli_local else Sys.which("gretlcli")

        old <- getwd()
        on.exit(setwd(old), add = TRUE)
        setwd(out_dir)

        if (!nzchar(gretlcli)) {
            message("Skipping .gdt creation: cannot find gretlcli (build gretl, or ensure gretlcli is on PATH).")
        } else {
            st1 <- system2(gretlcli, c("-b", "import_oecd_steel_annual.inp"))
            if (!file.exists(basename(annual_gdt))) {
                stop("Failed to create annual .gdt (see gretlcli output above).", call. = FALSE)
            }
            if (file.exists("import_oecd_steel_monthly.inp")) {
                st2 <- system2(gretlcli, c("-b", "import_oecd_steel_monthly.inp"))
                if (!file.exists(basename(monthly_gdt))) {
                    stop("Failed to create monthly .gdt (see gretlcli output above).", call. = FALSE)
                }
                invisible(st2)
            }
            invisible(st1)
        }
    }

    invisible(list(
        annual_csv = annual_csv,
        monthly_csv = monthly_csv
    ))
}

main <- function(argv) {
    out_dir <- if (length(argv) >= 1) argv[[1]] else "oecd_steel_out"
    make_gdt <- TRUE
    if (length(argv) >= 2) {
        make_gdt <- tolower(argv[[2]]) %in% c("1", "true", "yes", "y")
    }
    build_oecd_steel_panel(out_dir = out_dir, make_gdt = make_gdt, source = "dbnomics")
}

if (!interactive() && sys.nframe() == 0) {
    main(commandArgs(trailingOnly = TRUE))
}
