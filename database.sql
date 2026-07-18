CREATE TABLE public.app_config (
    id bigint NOT NULL,
    pin_code text NOT NULL
);
CREATE TABLE public.mixer (
    id bigint NOT NULL,
    is_on boolean DEFAULT false NOT NULL,
    power integer DEFAULT 0 NOT NULL
);
CREATE TABLE public.servo (
    id bigint NOT NULL,
    ang integer DEFAULT 0 NOT NULL
);
CREATE TABLE public.tempera (
    id bigint NOT NULL,
    temperature numeric DEFAULT 0.0 NOT NULL,
    updated_at timestamp with time zone DEFAULT now() NOT NULL
);
CREATE TABLE public.timers (
    id bigint NOT NULL,
    target_time timestamp with time zone,
    action boolean,
    is_active boolean DEFAULT false
);
CREATE TABLE public.toggle_status (
    id bigint NOT NULL,
    is_active boolean DEFAULT false NOT NULL
);


ALTER TABLE ONLY public.app_config ADD CONSTRAINT app_config_pkey PRIMARY KEY (id);
ALTER TABLE ONLY public.mixer ADD CONSTRAINT mixer_pkey PRIMARY KEY (id);
ALTER TABLE ONLY public.servo ADD CONSTRAINT servo_pkey PRIMARY KEY (id);
ALTER TABLE ONLY public.tempera ADD CONSTRAINT tempera_pkey PRIMARY KEY (id);
ALTER TABLE ONLY public.timers ADD CONSTRAINT timers_pkey PRIMARY KEY (id);
ALTER TABLE ONLY public.toggle_status ADD CONSTRAINT toggle_status_pkey PRIMARY KEY (id);


ALTER TABLE public.app_config ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.mixer ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.servo ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.tempera ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.toggle_status ENABLE ROW LEVEL SECURITY;


CREATE POLICY "Allow public read" ON public.app_config FOR SELECT USING (true);
CREATE POLICY "Allow public read" ON public.mixer FOR SELECT USING (true);
CREATE POLICY "Allow public read" ON public.servo FOR SELECT USING (true);
CREATE POLICY "Allow public read" ON public.tempera FOR SELECT USING (true);
CREATE POLICY "Allow public read" ON public.toggle_status FOR SELECT USING (true);
CREATE POLICY "Allow public update" ON public.app_config FOR UPDATE USING (true);
CREATE POLICY "Allow public update" ON public.mixer FOR UPDATE USING (true);
CREATE POLICY "Allow public update" ON public.servo FOR UPDATE USING (true);
CREATE POLICY "Allow public update" ON public.tempera FOR UPDATE USING (true);
CREATE POLICY "Allow public update" ON public.toggle_status FOR UPDATE USING (true);


COPY public.app_config (id, pin_code) FROM stdin;
1	1405
\.
COPY public.mixer (id, is_on, power) FROM stdin;
1	f	40
\.
COPY public.servo (id, ang) FROM stdin;
1	90
\.
COPY public.tempera (id, temperature, updated_at) FROM stdin;
1	30	2026-07-16 14:35:30+00
\.
COPY public.timers (id, target_time, action, is_active) FROM stdin;
1	2026-07-13 08:24:31.08+00	t	f
\.
COPY public.toggle_status (id, is_active) FROM stdin;
1	f
\.