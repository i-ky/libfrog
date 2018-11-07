#define _GNU_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yaml.h>

int frog_initialized;

yaml_document_t	config;
yaml_node_t	*root;

char	*last_search;
char	**objects;

static void	parse_configuration(FILE *config_file)
{
	yaml_parser_t	parser;

	yaml_parser_initialize(&parser);
	yaml_parser_set_input_file(&parser, config_file);

	if (0 != yaml_parser_load(&parser, &config))
	{
		if (NULL != (root = yaml_document_get_root_node(&config)))
		{
			if (YAML_MAPPING_NODE == root->type)
			{
				const yaml_node_pair_t	*pair;

				for (pair = root->data.mapping.pairs.start; pair < root->data.mapping.pairs.top; pair++)
				{
					const yaml_node_t	*key, *value;
					const yaml_node_pair_t	*nested_pair;

					key = yaml_document_get_node(&config, pair->key);

					if (YAML_SCALAR_NODE != key->type)
					{
						printf("All keys in root mapping must be scalar.\n");
						break;
					}

					value = yaml_document_get_node(&config, pair->value);

					if (YAML_MAPPING_NODE != value->type)
					{
						printf("All values in root mapping must be mappings themselves.\n");
						break;
					}

					for (nested_pair = value->data.mapping.pairs.start;
							nested_pair < value->data.mapping.pairs.top; nested_pair++)
					{
						const yaml_node_t	*nested_key, *nested_value;

						nested_key = yaml_document_get_node(&config, nested_pair->key);
						nested_value = yaml_document_get_node(&config, nested_pair->value);

						if (YAML_SCALAR_NODE != nested_key->type ||
								YAML_SCALAR_NODE != nested_value->type)
						{
							printf("All keys and values in nested mapping must be scalar.\n");
							break;
						}
					}

					if (nested_pair < value->data.mapping.pairs.top)
						break;
				}

				if (pair == root->data.mapping.pairs.top)
				{
					printf("Configuration file has been successfully parsed.\n");
					frog_initialized = 1;
				}
			}
			else
				printf("Root node must be a mapping.\n");
		}
		else
			printf("There are no YAML documents in the configuration file.\n");
	}
	else
		printf("Cannot parse configuration file. Please check if it is a valid YAML file.\n");

	yaml_parser_delete(&parser);
}

__attribute__((constructor))
static void	frog_initialize(void)
{
	const char	frog_config[] = "LIBFROG_CONFIG", *config_file_name;
	FILE		*config_file;

	printf("Initializing libfrog...\n");

	if (NULL != (config_file_name = secure_getenv(frog_config)))
	{
		printf("Reading configuration from \"%s\"...\n", config_file_name);

		if (NULL != (config_file = fopen(config_file_name, "r")))
		{
			parse_configuration(config_file);

			if (0 != fclose(config_file))
				printf("Failure to close \"%s\": %s.\n", config_file_name, strerror(errno));
		}
		else
			printf("Failure to open \"%s\": %s.\n", config_file_name, strerror(errno));
	}
	else
		printf("Path to configuration file must be set using \"%s\" environment variable.\n", frog_config);
}

__attribute__((destructor))
static void	frog_deinitialize()
{
	if (!frog_initialized)
		return;

	yaml_document_delete(&config);
	free(last_search);
	free(objects);
	frog_initialized = 0;
}

unsigned int	la_version(unsigned int version)
{
	/* NOTE: returning 0 causes Segmentation fault while should cause library to be ignored according to man */
	return frog_initialized ? version : 0;
}

char	*la_objsearch(const char *name, uintptr_t *cookie, unsigned int flag)
{
	printf("Searching %s\n", name);
	free(last_search);
	last_search = strdup(name);
	return name;
}

unsigned int	la_objopen(struct link_map *map, Lmid_t lmid, uintptr_t *cookie)
{
	static uintptr_t	index;
	unsigned int		audit = 0;

	*cookie = index++;
	objects = realloc(objects, sizeof(char *) * index);
	objects[index - 1] = last_search;

	if (NULL != last_search)
	{
		const yaml_node_pair_t	*pair;
		size_t			name_length;

		name_length = strlen(last_search);

		for (pair = root->data.mapping.pairs.start; pair < root->data.mapping.pairs.top; pair++)
		{
			const yaml_node_t	*key, *value;
			const yaml_node_pair_t	*nested_pair;

			key = yaml_document_get_node(&config, pair->key);

			if (name_length >= key->data.scalar.length &&
					0 == memcmp(last_search + name_length - key->data.scalar.length,
							key->data.scalar.value, key->data.scalar.length))
			{
				audit |= LA_FLG_BINDFROM;
			}

			value = yaml_document_get_node(&config, pair->value);

			for (nested_pair = value->data.mapping.pairs.start;
					nested_pair < value->data.mapping.pairs.top; nested_pair++)
			{
				const yaml_node_t	*nested_key;

				nested_key = yaml_document_get_node(&config, nested_pair->key);

				if (name_length >= nested_key->data.scalar.length &&
						0 == memcmp(last_search + name_length - nested_key->data.scalar.length,
								nested_key->data.scalar.value, nested_key->data.scalar.length))
				{
					audit |= LA_FLG_BINDTO;
				}
			}
		}

		printf("Will%s audit references %s %s.\n",
				audit ? "" : " not",
				audit & LA_FLG_BINDTO ?
						audit & LA_FLG_BINDFROM ? "to and from" : "to" :
						audit & LA_FLG_BINDFROM ? "from" : "to or from",
				last_search);
		last_search = NULL;
	}

	return audit;
}

uintptr_t	la_symbind64(Elf64_Sym *sym, unsigned int ndx, uintptr_t *refcook, uintptr_t *defcook,
		unsigned int *flags, const char *symname)
{
	const yaml_node_pair_t	*pair;
	const char		*ref_name, *def_name;
	size_t			ref_len, def_len;

	if (NULL == (ref_name = objects[*refcook]))
		return sym->st_value;

	ref_len = strlen(ref_name);

	if (NULL == (def_name = objects[*defcook]))
		return sym->st_value;

	def_len = strlen(def_name);

	printf("Binding %s referenced from %s and defined in %s\n", symname, ref_name, def_name);

	for (pair = root->data.mapping.pairs.start; pair < root->data.mapping.pairs.top; pair++)
	{
		const yaml_node_t	*key;

		key = yaml_document_get_node(&config, pair->key);

		if (ref_len >= key->data.scalar.length &&
				0 == memcmp(ref_name + ref_len - key->data.scalar.length,
						key->data.scalar.value, key->data.scalar.length))
		{
			const yaml_node_t	*value;
			const yaml_node_pair_t	*nested_pair;

			value = yaml_document_get_node(&config, pair->value);

			for (nested_pair = value->data.mapping.pairs.start;
					nested_pair < value->data.mapping.pairs.top; nested_pair++)
			{
				const yaml_node_t	*nested_key;

				nested_key = yaml_document_get_node(&config, nested_pair->key);

				if (def_len >= nested_key->data.scalar.length &&
						0 == memcmp(def_name + def_len - nested_key->data.scalar.length,
								nested_key->data.scalar.value, nested_key->data.scalar.length))
				{
					const yaml_node_t	*nested_value;
					char			lib_name[1000] = {0};
					void			*lib, *symbol;

					printf("%s (%u) referenced from object %p (%s) is incorrectly bound"
							" to address %p from object %p (%s)\n",
							symname, ndx, *refcook, objects[*refcook],
							sym->st_value, *defcook, objects[*defcook]);
					printf("This binding has to be fixed!\n");

					nested_value = yaml_document_get_node(&config, nested_pair->value);
					memcpy(lib_name, nested_value->data.scalar.value, nested_value->data.scalar.length);

					if (NULL == (lib = dlmopen(LM_ID_BASE, lib_name, RTLD_NOW | RTLD_NOLOAD)))
						printf("dlmopen() failed: %s.\n", strerror(errno));
					else if (NULL == (symbol = dlsym(lib, symname)))
						printf("dlsym() failed: %s.\n", strerror(errno));
					else
						return symbol;
				}
			}
		}
	}

	return sym->st_value;	
}
