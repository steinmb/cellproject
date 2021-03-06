<?php

/**
 * Implementation of MigrateSource, to handle imports from the CELL's OAI-PMH
 * sources.
 */
class CellMigrateSourceOaiPmh extends MigrateSourceOaiPmh {

  /**
   * Implementation of MigrateSource::getNextRow().
   *
   * Return the next line of the OAI-PMH source as a SimpleXMLElement.
   */
  public function getNextRow() {
    $row = new stdClass();
    $row->xml = $this->sourceRecords->nextItem();
    if (!$row->xml) {
      return;
    }

    // Retrieve and set the row's key.
    $source_key = $this->activeMap->getSourceKey();
    $key_name = key($source_key);
    $result = $row->xml->xpath('header/identifier');
    if (!$result || !(string)$result[0]) {
      return;
    }
    $row->$key_name = (string)$result[0];

    // Register namespaces for row.
    $namespaces = $row->xml->getNamespaces(TRUE);
    foreach ($namespaces as $prefix => $url) {
      if (empty($prefix)) {
        // XPath does not allow empty prefixes.
        if ($url == 'http://www.loc.gov/mods/v3') {
          $prefix = 'mods';
        }
        else {
          $prefix = 'unknown';
        }
      }
      $register_result = $row->xml->registerXPathNamespace($prefix, $url);
    }
    return $row;
  }
}

/**
 * Parent class with common stuff for all our migrations.
 */
class CellOaiPmhMigration extends XMLMigration {

  public $fields = NULL;

  public function __construct($arguments) {
    parent::__construct($arguments);

    // There isn't a consistent way to automatically identify appropriate
    // "fields" from an XML feed, so we pass an explicit list of source fields.
    $this->fields = array(
      'identifier' => 'header/identifier',
      'mods/recordInfo/recordContentSource' => 'mods/recordInfo/recordContentSource',
      'mods/titleInfo/title' => 'mods/titleInfo/title',
      'mods/originInfo/dateIssued' => 'mods/originInfo/dateIssued',
      'mods/language/languageTerm' => 'mods/language/languageTerm',
      'mods/location/url' => 'mods/location/url',
      'mods/identifier' => 'mods/identifier',
      'mods/extension/recordInfo/name/namePart' => 'mods/extension/recordInfo/name/namePart',
      'mods/physicalDescription/form/technology' => 'mods/physicalDescription/form/technology',
      'mods/physicalDescription/form/publication-type' => t('mods/physicalDescription/form/publication-type (with authority)'),
      'mods/physicalDescription/form/publication-type-complementary' => t('mods/physicalDescription/form/publication-type (without authority)'),
      'mods/genre/procedural-modality' => t('mods/genre/procedural-modality (with authority)'),
      'mods/genre/procedural-modality-complementary' => t('mods/genre/procedural-modality (without authority)'),
      'mods/physicalDescription/form/mechanism' => t('mods/physicalDescription/form/mechanism (with authority)'),
      'mods/physicalDescription/form/mechanism-complementary' => t('mods/physicalDescription/form/mechanism (without authority)'),
      'mods/physicalDescription/form/format' => t('mods/physicalDescription/form/format (with authority)'),
      'mods/physicalDescription/form/format-complementary' => t('mods/physicalDescription/form/format (without authority)'),
      'mods/abstract' => 'mods/abstract',
      'mods/genre/literary-quality' => 'mods/genre/literary-quality',
      'mods/recordInfo/recordCreationDate' => 'mods/recordInfo/recordCreationDate',
      'mods/recordInfo/recordChangeDate' => 'mods/recordInfo/recordChangeDate',
      'mods/recordInfo/languageOfCataloging/languageTerm' => 'mods/recordInfo/languageOfCataloging/languageTerm',
    );
  }

  public function prepareRow($row) {
    if (parent::prepareRow($row) === FALSE) {
      return FALSE;
    }

    // Reject row if mandatory elements are missing.
    if (!$this->applyXpath($row, 'header/identifier')) {
      return FALSE;
    }
    if (!$this->applyXpath($row, 'metadata/mods:mods/mods:titleInfo/mods:title')) {
      return FALSE;
    }
    if (!$this->applyXpath($row, 'metadata/mods:mods/mods:identifier[@type="uri"]')) {
      return FALSE;
    }

    // Note: No need to track changes using the highwaterField.
    // MigrateSourceOaiPmh uses the hash to determine whether the record has
    // changed.
  }

  protected function convertW3CDtfToYear($value) {
    if (is_string($value)) {
      $date = date_parse_from_format('Y-!m-!d !H:!i:!s', $value);
      return $date['year'];
    }
  }

  protected function validateUrl($value) {
    if (is_array($value)) {
      foreach ($value as $k => $v) {
        if (!valid_url($v, TRUE)) {
          $value[$k] = '';
        }
      }
    }
    elseif (!valid_url($value, TRUE)) {
      $value = '';
    }
    return $value;
  }

  protected function decodeEntities($value) {
    if (is_array($value)) {
      foreach ($value as $k => $v) {
        $value[$k] = decode_entities($v);
      }
    }
    else {
      $value = decode_entities($value);
    }
    return $value;
  }
}

class CellNodeMigration extends CellOaiPmhMigration {

  public function __construct($arguments) {
    parent::__construct($arguments);

    $this->description = t('OAI-PMH harvest of CELL data into nodes.');
    $this->source = new CellMigrateSourceOaiPmh($arguments['url'], 'mods', NULL, NULL, $arguments['set'], $this->fields, array('skip_count' => TRUE, 'cache_counts' => FALSE));
    $this->destination = new MigrateDestinationNode('work');
    $this->map = new MigrateSQLMap($this->machineName,
      array(
        'identifier' => array(
          'type' => 'varchar',
          'length' => 255,
          'not null' => TRUE,
          'description' => 'Record identifier'
        )
      ),
      MigrateDestinationNode::getKeySchema()
    );

    $node_options = MigrateDestinationNode::options(LANGUAGE_NONE, 'plain_text');

    $this->addFieldMapping('field_source_entry_oai_id', 'identifier')
      ->xpath('header/identifier');

    $this->addFieldMapping('field_source', 'mods/recordInfo/recordContentSource')
      ->xpath('metadata/mods:mods/mods:recordInfo/mods:recordContentSource[@authorityURI="http://cellproject.net/authorities/source-database"]')
      ->callbacks(array($this, 'decodeEntities'));

    $this->addFieldMapping('title', 'mods/titleInfo/title')
      ->xpath('metadata/mods:mods/mods:titleInfo/mods:title')
      ->callbacks(array($this, 'decodeEntities'));

    $this->addFieldMapping('field_year', 'mods/originInfo/dateIssued')
      ->xpath('metadata/mods:mods/mods:originInfo/mods:dateIssued[@encoding="w3cdtf"]')
      ->callbacks(array($this, 'convertW3CDtfToYear'));

    $this->addFieldMapping('field_language', 'mods/language/languageTerm')
      ->xpath('metadata/mods:mods/mods:language/mods:languageTerm[@authority="rfc3066"]');

    $this->addFieldMapping('field_project_url', 'mods/location/url')
      ->xpath('metadata/mods:mods/mods:location/mods:url')
      ->callbacks(array($this, 'validateUrl'));

    $this->addFieldMapping('field_entry_url', 'mods/identifier')
      ->xpath('metadata/mods:mods/mods:identifier[@type="uri"]')
      ->callbacks(array($this, 'validateUrl'));

    $this->addFieldMapping('field_author_source_entry', 'mods/extension/recordInfo/name/namePart')
      ->xpath('metadata/mods:mods/mods:extension/cell:recordInfo/mods:name/mods:namePart')
      ->callbacks(array($this, 'decodeEntities'));

    $this->addFieldMapping('field_technology_used', 'mods/physicalDescription/form/technology')
      ->xpath('metadata/mods:mods/mods:physicalDescription/mods:form[@type="technology" and @authorityURI="http://cellproject.net/authorities/technology"]')
      ->callbacks(array($this, 'decodeEntities'));

    $this->addFieldMapping('field_media_format', 'mods/physicalDescription/form/publication-type')
      ->xpath('metadata/mods:mods/mods:physicalDescription/mods:form[@type="publication-type" and @authorityURI="http://cellproject.net/authorities/publication-type"]')
      ->callbacks(array($this, 'decodeEntities'));

    $this->addFieldMapping('field_complementary_publication', 'mods/physicalDescription/form/publication-type-complementary')
      ->xpath('metadata/mods:mods/mods:physicalDescription/mods:form[@type="publication-type" and not(@authorityURI="http://cellproject.net/authorities/publication-type")]')
      ->callbacks(array($this, 'decodeEntities'));

    $this->addFieldMapping('field_interactive_modalities', 'mods/genre/procedural-modality')
      ->xpath('metadata/mods:mods/mods:genre[@type="procedural-modality" and @authorityURI="http://cellproject.net/authorities/procedural-modality"]')
      ->callbacks(array($this, 'decodeEntities'));

    $this->addFieldMapping('field_complementary_procedural', 'mods/genre/procedural-modality-complementary')
      ->xpath('metadata/mods:mods/mods:genre[@type="procedural-modality" and not(@authorityURI="http://cellproject.net/authorities/procedural-modality")]')
      ->callbacks(array($this, 'decodeEntities'));

    $this->addFieldMapping('field_mechanisms', 'mods/physicalDescription/form/mechanism')
      ->xpath('metadata/mods:mods/mods:physicalDescription/mods:form[@type="mechanism" and @authorityURI="http://cellproject.net/authorities/mechanism"]')
      ->callbacks(array($this, 'decodeEntities'));

    $this->addFieldMapping('field_complementary_mechanism', 'mods/physicalDescription/form/mechanism-complementary')
      ->xpath('metadata/mods:mods/mods:physicalDescription/mods:form[@type="mechanism" and not(@authorityURI="http://cellproject.net/authorities/mechanism")]')
      ->callbacks(array($this, 'decodeEntities'));

    $this->addFieldMapping('field_formal_aspects', 'mods/physicalDescription/form/format')
      ->xpath('metadata/mods:mods/mods:physicalDescription/mods:form[@type="format" and @authorityURI="http://cellproject.net/authorities/format"]')
      ->callbacks(array($this, 'decodeEntities'));

    $this->addFieldMapping('field_complementary_formal', 'mods/physicalDescription/form/format-complementary')
      ->xpath('metadata/mods:mods/mods:physicalDescription/mods:form[@type="format" and not(@authorityURI="http://cellproject.net/authorities/format")]')
      ->callbacks(array($this, 'decodeEntities'));

    $this->addFieldMapping('field_literary_quality_ies_', 'mods/genre/literary-quality')
      ->xpath('metadata/mods:mods/mods:genre[@type="literary-quality"]')
      ->callbacks(array($this, 'decodeEntities'));

    $this->addFieldMapping('body', 'mods/abstract')
      ->xpath('metadata/mods:mods/mods:abstract')
      ->callbacks(array($this, 'decodeEntities'));

    $this->addFieldMapping('created', 'mods/recordInfo/recordCreationDate') // Note: Migrate does strtotime() for this field by default.
      ->xpath('metadata/mods:mods/mods:recordInfo/mods:recordCreationDate[@encoding="iso8601"]');

    $this->addFieldMapping('changed', 'mods/recordInfo/recordChangeDate') // Note: Migrate does strtotime() for this field by default.
      ->xpath('metadata/mods:mods/mods:recordInfo/mods:recordChangeDate[@encoding="iso8601"]');

    $this->addFieldMapping('field_source_entry_language', 'mods/recordInfo/languageOfCataloging/languageTerm')
      ->xpath('metadata/mods:mods/mods:recordInfo/mods:languageOfCataloging/mods:languageTerm[@authority="rfc3066"]')
      ->callbacks(array($this, 'decodeEntities'));

    // The following fields are processed manually on complete().
    $this->addFieldMapping('field_primary_creator', NULL);
    $this->addFieldMapping('field_publisher', NULL);
  }

  public function complete($entity, $row) {
    // Note: Xpath queries such as 'metadata/mods:mods/mods:name[1]/mods:namePart'
    // fail with SimpleXMLElement::xpath(), not sure why! Also having trouble
    // querying subnodes. Making ugly code instead.

    // Map Primary Creator(s) to field collection.
    $creators = $row->xml->xpath('metadata/mods:mods/mods:name');
    if ($creators && count($creators)) {
      foreach ($creators as $creator) {
        $creator_name = '';
        $creator_href = '';

        if ($creator && isset($creator->namePart)) {
          $creator_name = decode_entities((string) $creator->namePart);
          $attributes = $creator->attributes('http://www.w3.org/1999/xlink'); // Full namespace needed here.
          if ($attributes && isset($attributes['href'])) {
            $creator_href = (string) $attributes['href'];
          }
          if ($creator_name) {
            $this->_set_primary_creator_field_collection($entity, $creator_name, $creator_href);
          }
        }
      }
    }

    // Map Publisher(s) to field collection.
    // Check for CELL publisher extension.
    $publishers = $row->xml->xpath('metadata/mods:mods/mods:extension/cell:originInfo/cell:publisher');
    if ($publishers && count($publishers)) {
      foreach ($publishers as $publisher) {
        $publisher_name = decode_entities((string) $publisher);
        $publisher_href = '';
        $attributes = $publisher->attributes('http://www.w3.org/1999/xlink'); // Full namespace needed here.
        if ($attributes && isset($attributes['href'])) {
          $publisher_href = (string) $attributes['href'];
        }
        if ($publisher_name) {
          $this->_set_publisher_field_collection($entity, $publisher_name, $publisher_href);
        }
      }
    }
    else {
      // Fallback on standard MODS publisher element.
      $publishers = $row->xml->xpath('metadata/mods:mods/mods:originInfo/mods:publisher');
      if ($publishers && count($publishers)) {
        foreach ($publishers as $publisher) {
          $publisher_name = decode_entities((string) $publisher);
          $publisher_href = '';
          if ($publisher_name) {
            $this->_set_publisher_field_collection($entity, $publisher_name, $publisher_href);
          }
        }
      }
    }
  }

  protected function _set_primary_creator_field_collection($entity, $name, $href) {
    $values = array(
      'field_name' => 'field_primary_creator',
      'field_artist' => array(LANGUAGE_NONE => array(array('value' => $name))),
      'field_creator_uri' => array(LANGUAGE_NONE => array(array('value' => $href))),
    );
    $field_collection_item = entity_create('field_collection_item', $values);
    $field_collection_item->setHostEntity('node', $entity);
    $field_collection_item->save();
  }

  protected function _set_publisher_field_collection($entity, $name, $href) {
    $values = array(
      'field_name' => 'field_publisher',
      'field_publisher_people' => array(LANGUAGE_NONE => array(array('value' => $name))),
      'field_publisher_uri' => array(LANGUAGE_NONE => array(array('value' => $href))),
    );
    $field_collection_item = entity_create('field_collection_item', $values);
    $field_collection_item->setHostEntity('node', $entity);
    $field_collection_item->save();
  }
}
